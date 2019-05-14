#include <boost/interprocess/streams/bufferstream.hpp>
#include <sstream>
#include <fstream>

#include "rescore_common.hpp"
#include "rescore_message.hpp"
#include "rescore_dispatch.hpp"

#include "lat/lattice-functions.h"
#include "lat/compose-lattice-pruned.h"
#include "lm/const-arpa-lm.h"
#include <fst/script/project.h>
#include "nnet3/nnet-utils.h"

using namespace boost::interprocess;
using namespace kaldi;


class LatticeRescoreTask {
public:
    LatticeRescoreTask(
            CompactLattice *lattice,
            RescoreJobPtr session,
            ConstArpaLm *rescore_lm,
            fst::VectorFst<fst::StdArc> *std_lm_fst,
            kaldi::nnet3::Nnet *rnnlm,
            CuMatrix<BaseFloat> *rnnlm_embedding_matrix,
            rnnlm::RnnlmComputeStateComputationOptions rnnlm_opts,
            kaldi::int32 max_ngram_order,
            bool do_carpa_rescore,
            bool do_rnnlm_rescore,
            BaseFloat acoustic_scale);

    void operator()(); // The decoding happens here.
    ~LatticeRescoreTask(); // Output happens here.

private:
    bool rescore_lattice_carpa(CompactLattice *clat, CompactLattice *result_lat);

    CompactLattice *rescore_lattice_rnnlm(CompactLattice *clat);

    void reload_lm_fst();

    // The following variables correspond to inputs:
    CompactLattice *inlat_; // Stored input.
    RescoreJobPtr session_;
    BaseFloat acoustic_scale_;
    // models and stuff
    // decode lm
    fst::VectorFst<fst::StdArc> *std_lm_fst_;
    // carpa
    ConstArpaLm *rescore_lm_;
    // rnnlm
    rnnlm::RnnlmComputeStateComputationOptions rnnlm_opts;
    kaldi::nnet3::Nnet *rnnlm;
    CuMatrix<BaseFloat> *rnnlm_embedding_matrix;
    kaldi::int32 max_ngram_order;
    // rescore types to be performed
    const bool do_carpa_rescore;
    const bool do_rnnlm_rescore;
    // intermediary
    fst::MapFst<fst::StdArc, LatticeArc, fst::StdToLatticeMapper<BaseFloat>> *lm_fst_;

    bool computed_;
    CompactLattice *outlat_; // Stored output.
};

LatticeRescoreTask::LatticeRescoreTask(
        CompactLattice *lattice,
        RescoreJobPtr session,
        ConstArpaLm *rescore_lm,
        fst::VectorFst<fst::StdArc> *std_lm_fst,
        kaldi::nnet3::Nnet *rnnlm,
        CuMatrix<BaseFloat> *rnnlm_embedding_matrix,
        rnnlm::RnnlmComputeStateComputationOptions rnnlm_opts,
        kaldi::int32 max_ngram_order,
        bool do_carpa_rescore,
        bool do_rnnlm_rescore,
        BaseFloat acoustic_scale)
        : inlat_(lattice),
          session_(std::move(session)),
          acoustic_scale_(acoustic_scale),
          std_lm_fst_(std_lm_fst),
          rescore_lm_(rescore_lm),
          rnnlm_opts(rnnlm_opts),
          rnnlm(rnnlm),
          rnnlm_embedding_matrix(rnnlm_embedding_matrix),
          max_ngram_order(max_ngram_order),
          do_carpa_rescore(do_carpa_rescore),
          do_rnnlm_rescore(do_rnnlm_rescore),
          lm_fst_(nullptr),
          computed_(false),
          outlat_(nullptr) {
}

/**
*** Reload lm fst
**/
void LatticeRescoreTask::reload_lm_fst() {
    if (lm_fst_) {
        delete lm_fst_;
        lm_fst_ = nullptr;
    }
    // this is from lattice-lmrescore.cc

    // mapped_fst is the LM fst interpreted using the LatticeWeight semiring,
    // with all the cost on the first member of the pair (since it's a graph
    // weight).
    size_t num_states_cache = 50000;
    fst::CacheOptions cache_opts(true, num_states_cache);
    fst::MapFstOptions mapfst_opts(cache_opts);
    fst::StdToLatticeMapper<BaseFloat> mapper;
    lm_fst_ = new fst::MapFst<fst::StdArc, LatticeArc,
            fst::StdToLatticeMapper<BaseFloat>>(*std_lm_fst_, mapper, mapfst_opts);
}

void LatticeRescoreTask::operator()() {
    reload_lm_fst();

    outlat_ = new CompactLattice();
    bool carpa_success = false;
    if (do_carpa_rescore) {
        if (rescore_lattice_carpa(inlat_, outlat_)) {
            // We'll write the lattice without acoustic scaling.
            if (acoustic_scale_ != 0.0) {
                fst::ScaleLattice(fst::AcousticLatticeScale(1.0 / acoustic_scale_), outlat_);
            }
            carpa_success = true;
        } else if (!do_rnnlm_rescore) {
            // report error and use output the same lattice without rescoring
            delete outlat_; // don't forget to free recently allocated outlat_
            outlat_ = inlat_;
            KALDI_WARN << "Lattice rescoring by CARPA failed. Outputting the same lattice";
        }
    }
    if (do_rnnlm_rescore && (!do_carpa_rescore || carpa_success)) {
        // do rnnlm rescoring
        // depending on whether we did carpa rescore, use carpa rescored lattice as input
        CompactLattice *rnnlm_inlat = inlat_;
        if (do_carpa_rescore && carpa_success) {
            rnnlm_inlat = outlat_;
        }
        // rnnlm pruned rescoring needs acoustic scale to be meaningful
        acoustic_scale_ = 0.1;
        CompactLattice *rnnlm_outlat = rescore_lattice_rnnlm(rnnlm_inlat);
        if (rnnlm_outlat) {
            delete outlat_;
            outlat_ = rnnlm_outlat;
        } else {
            KALDI_WARN << "Lattice rescoring by RNNLM failed. Outputting the same lattice";
            // check if carpa also failed or wasn't run
            if (!do_carpa_rescore || !carpa_success) {
                // nothing succeeded, remove the waste
                delete outlat_;
            }
            outlat_ = rnnlm_inlat;
        }
    }

    computed_ = true;

    // Output lattice
    RescoreMessage *out = new RescoreMessage();
    out->body_length(out->max_body_length);
    obufferstream str(out->body(), out->body_length());
    if (!WriteCompactLattice(str, true, *outlat_)) {
        KALDI_WARN << "Failed to write lattice. Stream pos: " << str.tellp();
        // reset buffer
        str.buffer(out->body(), out->body_length());
        // send the unrescored lattice back
        WriteCompactLattice(str, true, *inlat_);
        out->body_length(str.tellp());
    }
    out->body_length(str.tellp());
    out->encode_header();

    delete inlat_; // inlat_ is no longer needed, free allocated memory

    session_->deliver(out); // session will take ownership of the message
}

/**
*** Rescore lattice
**/
bool LatticeRescoreTask::rescore_lattice_carpa(CompactLattice *clat, CompactLattice *result_lat) {
    // This here is roughly from lattice-lmrescore-const-arpa.cc

    Lattice tmp_lattice;
    ConvertLattice(*clat, &tmp_lattice);
    // Before composing with the LM FST, we scale the lattice weights
    // by the inverse of "lm_scale".  We'll later scale by "lm_scale".
    // We do it this way so we can determinize and it will give the
    // right effect (taking the "best path" through the LM) regardless
    // of the sign of lm_scale.
    fst::ScaleLattice(fst::GraphLatticeScale(-1.0), &tmp_lattice);
    ArcSort(&tmp_lattice, fst::OLabelCompare<LatticeArc>());

    Lattice composed_lat;
    // the compose cache bit is roughly from lattice-lmrescore.cc
    // Change the options for TableCompose to match the input
    // (because it's the arcs of the LM FST we want to do lookup
    // on).
    fst::TableComposeOptions compose_opts(fst::TableMatcherOptions(),
                                          true, fst::SEQUENCE_FILTER,
                                          fst::MATCH_INPUT);

    // The following is an optimization for the TableCompose
    // composition: it stores certain tables that enable fast
    // lookup of arcs during composition.
    fst::TableComposeCache<fst::Fst<LatticeArc>> *lm_compose_cache_ =
            new fst::TableComposeCache<fst::Fst<LatticeArc>>(compose_opts);

    // Could just do, more simply: Compose(lat, lm_fst, &composed_lat);
    // and not have lm_compose_cache at all.
    // The command below is faster, though; it's constant not
    // logarithmic in vocab size.


    TableCompose(tmp_lattice, *(lm_fst_), &composed_lat, lm_compose_cache_);
    delete lm_compose_cache_; // no longer needed, maybe we can allocate it on the stack? but maybe not...

    Invert(&composed_lat); // make it so word labels are on the input.
    CompactLattice determinized_lat;
    DeterminizeLattice(composed_lat, &determinized_lat);
    fst::ScaleLattice(fst::GraphLatticeScale(-1.0), &determinized_lat);
    if (determinized_lat.Start() == fst::kNoStateId) {
        KALDI_ERR << "Empty lattice (incompatible LM?)";
        return false;
    } else {
        // this here is from lattice-lmrescore-const-arpa.cc
        fst::ScaleLattice(fst::GraphLatticeScale(1.0), &determinized_lat);
        ArcSort(&determinized_lat, fst::OLabelCompare<CompactLatticeArc>());

        // Wraps the ConstArpaLm format language model into FST. We re-create it
        // for each lattice to prevent memory usage increasing with time.
        ConstArpaLmDeterministicFst const_arpa_fst(*(rescore_lm_));

        // Composes lattice with language model.
        CompactLattice composed_clat;
        ComposeCompactLatticeDeterministic(determinized_lat,
                                           &const_arpa_fst, &composed_clat);

        // Determinizes the composed lattice.
        Lattice composed_lat;
        ConvertLattice(composed_clat, &composed_lat);
        Invert(&composed_lat);
        DeterminizeLattice(composed_lat, result_lat);
        fst::ScaleLattice(fst::GraphLatticeScale(1.0), result_lat);
        if (result_lat->Start() == fst::kNoStateId) {
            KALDI_ERR << "Empty lattice (incompatible LM?)";
            return false;
        }
    }
    return true;
}

CompactLattice *LatticeRescoreTask::rescore_lattice_rnnlm(CompactLattice *clat) { //, CompactLattice *result_lat) {
    // this here is roughly from lattice-lmrescore-kaldi-rnnlm-pruned.cc

    // hardcoded lm_scale for now
    BaseFloat lm_scale = 0.8;
    // lm to subtract
    // if we did carpa rescoring, we use carpa as the model to subtract
    // for G.carpa
    fst::DeterministicOnDemandFst<fst::StdArc> *carpa_lm_to_subtract_fst = nullptr;

    // for G.fst
    fst::ScaleDeterministicOnDemandFst *lm_to_subtract_det_scale = nullptr;
    fst::BackoffDeterministicOnDemandFst<fst::StdArc> *lm_to_subtract_det_backoff = nullptr;

    if (do_carpa_rescore) {
        carpa_lm_to_subtract_fst = new ConstArpaLmDeterministicFst(*rescore_lm_);
        lm_to_subtract_det_scale =
                new fst::ScaleDeterministicOnDemandFst(-lm_scale, carpa_lm_to_subtract_fst);
    } else {
        lm_to_subtract_det_backoff =
                new fst::BackoffDeterministicOnDemandFst<fst::StdArc>(*std_lm_fst_);
        lm_to_subtract_det_scale =
                new fst::ScaleDeterministicOnDemandFst(-lm_scale,
                                                       lm_to_subtract_det_backoff);
    }

    // this here wraps and handles rnnlm computations
    const rnnlm::RnnlmComputeStateInfo info(rnnlm_opts, *rnnlm, *rnnlm_embedding_matrix);
    rnnlm::KaldiRnnlmDeterministicFst *lm_to_add_orig =
            new rnnlm::KaldiRnnlmDeterministicFst(max_ngram_order, info);

    fst::DeterministicOnDemandFst<fst::StdArc> *lm_to_add =
            new fst::ScaleDeterministicOnDemandFst(lm_scale, lm_to_add_orig);

    // Before composing with the LM FST, we scale the lattice weights
    // by the inverse of "lm_scale".  We'll later scale by "lm_scale".
    // We do it this way so we can determinize and it will give the
    // right effect (taking the "best path" through the LM) regardless
    // of the sign of lm_scale.
    if (acoustic_scale_ != 1.0 && acoustic_scale_ != 0.0) {
        fst::ScaleLattice(fst::AcousticLatticeScale(acoustic_scale_), clat);
    }
    TopSortCompactLatticeIfNeeded(clat);

    fst::ComposeDeterministicOnDemandFst<fst::StdArc> combined_lms(
            lm_to_subtract_det_scale, lm_to_add);

    // Composes lattice with language model.
    ComposeLatticePrunedOptions compose_opts;
    CompactLattice *composed_clat = new CompactLattice();
    ComposeCompactLatticePruned(compose_opts, *clat,
                                &combined_lms, composed_clat);

    if (acoustic_scale_ != 1.0 && acoustic_scale_ != 0.0) {
        fst::ScaleLattice(fst::AcousticLatticeScale(1.0 / acoustic_scale_),
                          composed_clat);
    }

    // clean up
    delete lm_to_add;
    delete lm_to_add_orig;
    delete carpa_lm_to_subtract_fst;
    delete lm_to_subtract_det_scale;
    delete lm_to_subtract_det_backoff;

    if (composed_clat->NumStates() == 0) {
        // Something went wrong...
        KALDI_ERR << "Empty lattice (incompatible LM?)";
        delete composed_clat;
        return nullptr;
    }
    return composed_clat;
}

LatticeRescoreTask::~LatticeRescoreTask() {
    if (!computed_) {
        KALDI_ERR << "Destructor called without operator (), error in calling code.";
    }

    delete outlat_;
    delete lm_fst_;
}


class RescoreDispatch::impl {
public:
    impl(TaskSequencerConfig sequencer_config,
         const std::string &rescore_mode,
         const std::string &lm_fst_rspecifier,
         const std::string &carpa_rspecifier,
         const std::string &rnnlm_dir,
         kaldi::int32 max_ngram_order,
         const bool do_carpa_rescore,
         const bool do_rnnlm_rescore
    )
            : sequencer(sequencer_config),
              acoustic_scale(0),
              max_ngram_order(max_ngram_order),
              do_carpa_rescore(do_carpa_rescore),
              do_rnnlm_rescore(do_rnnlm_rescore) {
        // load LM used for decoding
        load_lm_fst(lm_fst_rspecifier);
        // depending on mode, load the stuff we need
        if (do_carpa_rescore) {
            // load carpa for rescoring
            rescore_lm_ = new ConstArpaLm();
            ReadKaldiObject(carpa_rspecifier, rescore_lm_);
        }
        if (do_rnnlm_rescore) {
            // load rnnlm for rescoring
            rnnlm = new kaldi::nnet3::Nnet();
            ReadKaldiObject(rnnlm_dir + "/final.raw", rnnlm);

            // determine whether word_embedding or feat_embedding is used here...
            std::string embedding = rnnlm_dir + "/word_embedding.final.mat";
            if (!file_exists(embedding)) {
                // no word_embedding, we assume feat_embedding.final.mat, then
                embedding = rnnlm_dir + "/feat_embedding.final.mat";
            }
            embedding_mat = new CuMatrix<BaseFloat>();
            ReadKaldiObject(embedding, embedding_mat);

            KALDI_ASSERT(IsSimpleNnet(*rnnlm));

            // also create & load rnnlm::RnnlmComputeStateComputationOptions
            rnnlm_opts = rnnlm::RnnlmComputeStateComputationOptions();
            std::string special_symbol_opts = read_into_string(rnnlm_dir + "/special_symbol_opts.txt");
            std::vector<std::string> split_by_space = split(special_symbol_opts, " ");
            for (std::string &str: split_by_space) {
                if (str.find("eos-symbol") != std::string::npos) {
                    rnnlm_opts.eos_index = std::stoi(split(str, "=").back());
                } else if (str.find("bos-symbol") != std::string::npos) {
                    rnnlm_opts.bos_index = std::stoi(split(str, "=").back());
                }
            }
            KALDI_LOG << "bos-symbol=" << rnnlm_opts.bos_index;
            KALDI_LOG << "eos-symbol=" << rnnlm_opts.eos_index;
        }
    };

    void rescore(const RescoreMessage &msg, RescoreJobPtr const &session) {
        ibufferstream input_stream(msg.body(), msg.body_length());
        CompactLattice *lat = nullptr;

        if (ReadCompactLattice(input_stream, true, &lat)) {
            // rescore lattice
            // LatticeRescoreTask will take ownership of lat
            LatticeRescoreTask *task = new LatticeRescoreTask(lat,
                                                              session,
                                                              rescore_lm_,
                                                              std_lm_fst_,
                                                              rnnlm,
                                                              embedding_mat,
                                                              rnnlm_opts,
                                                              max_ngram_order,
                                                              do_carpa_rescore,
                                                              do_rnnlm_rescore,
                                                              acoustic_scale);

            sequencer.Run(task); // takes ownership of "task",
            // and will delete it when done.
        } else {
            KALDI_ERR << "Failed to read lattice";
            session->close();
        }
    }

private:
    TaskSequencer<LatticeRescoreTask> sequencer;
    BaseFloat acoustic_scale;
    kaldi::int32 max_ngram_order;
    bool do_carpa_rescore;
    bool do_rnnlm_rescore;

    // decode lm fst
    fst::VectorFst<fst::StdArc> *std_lm_fst_ = nullptr;
    // carpa rescore lm
    ConstArpaLm *rescore_lm_ = nullptr;
    // rnnlm
    kaldi::nnet3::Nnet *rnnlm = nullptr;
    CuMatrix<BaseFloat> *embedding_mat = nullptr;
    rnnlm::RnnlmComputeStateComputationOptions rnnlm_opts;

    void load_lm_fst(const std::string &lm_fst_file) {
        try {
            fst::script::MutableFstClass *fst =
                    fst::script::MutableFstClass::Read(lm_fst_file, true);
            fst::script::Project(fst, fst::PROJECT_OUTPUT);

            const fst::Fst<fst::StdArc> *tmp_fst = fst->GetFst<fst::StdArc>();

            std_lm_fst_ = new fst::VectorFst<fst::StdArc>(*tmp_fst);

            if (std_lm_fst_->Properties(fst::kILabelSorted, true) == 0) {
                // Make sure LM is sorted on ilabel.
                fst::ILabelCompare<fst::StdArc> ilabel_comp;
                fst::ArcSort(std_lm_fst_, ilabel_comp);
            }

            delete fst;
        } catch (std::runtime_error &e) {
            KALDI_ERR << "Error loading the FST decoding graph: " << lm_fst_file;
        }
    }

    /// Checks if a file with the given name exists
    bool file_exists(const std::string &name) {
        if (FILE *file = fopen(name.c_str(), "r")) {
            fclose(file);
            return true;
        } else {
            return false;
        }
    }

    /// reads the file at the given path into a string...
    /// adapted from: https://stackoverflow.com/a/2912614
    std::string read_into_string(const std::string &path) {
        std::ifstream ifs(path);
        std::string out((std::istreambuf_iterator<char>(ifs)),
                        (std::istreambuf_iterator<char>()));
        return out;
    }

    /// Splits a string by the given delimiter string.
    /// adapted from: https://thispointer.com/how-to-split-a-string-in-c/
    std::vector<std::string> split(const std::string &string_to_split, const std::string &delimiter) {
        std::vector<std::string> out;
        unsigned long long int start_idx = 0;
        unsigned long long int end_idx = 0;
        const unsigned long long int delimiter_size = delimiter.size();
        while ((end_idx = string_to_split.find(delimiter, start_idx)) < string_to_split.size()) {
            std::string val = string_to_split.substr(start_idx, end_idx - start_idx);
            out.push_back(val);
            start_idx = end_idx + delimiter_size;
        }
        if (start_idx < string_to_split.size()) {
            std::string val = string_to_split.substr(start_idx);
            out.push_back(val);
        }
        return out;
    }
};

RescoreDispatch::RescoreDispatch(TaskSequencerConfig &sequencer_config,
                                 const std::string &rescore_mode,
                                 const std::string &lm_fst_rspecifier,
                                 const std::string &carpa_rspecifier,
                                 const std::string &rnnlm_dir,
                                 kaldi::int32 max_ngram_order,
                                 const bool do_carpa_rescore,
                                 const bool do_rnnlm_rescore)
        : pimpl(new impl(sequencer_config,
                         rescore_mode,
                         lm_fst_rspecifier,
                         carpa_rspecifier,
                         rnnlm_dir,
                         max_ngram_order,
                         do_carpa_rescore,
                         do_rnnlm_rescore)) {
}

RescoreDispatch::~RescoreDispatch() {
    delete pimpl;
}

void RescoreDispatch::rescore(const RescoreMessage &msg, const RescoreJobPtr session) {
    pimpl->rescore(msg, session);
}
