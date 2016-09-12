#include <boost/interprocess/streams/bufferstream.hpp>
#include <sstream>

#include "rescore_common.hpp"
#include "rescore_message.hpp"
#include "rescore_dispatch.hpp"

#include "base/kaldi-common.h"
#include "thread/kaldi-task-sequence.h"
#include "lat/lattice-functions.h"
#include "lm/const-arpa-lm.h"
#include <fst/script/project.h>

using namespace boost::interprocess;
using namespace kaldi;


class LatticeRescoreTask {
    public:
    // Initializer sets various variables.
    LatticeRescoreTask(
        CompactLattice* lattice,
        rescore_job_ptr session,
        ConstArpaLm* rescore_lm,
        fst::VectorFst<fst::StdArc>* std_lm_fst,
        BaseFloat acoustic_scale);  
    void operator () (); // The decoding happens here.
    ~LatticeRescoreTask(); // Output happens here.

    private:
    bool rescore_lattice(CompactLattice* clat, CompactLattice* result_lat);
    void reload_lm_fst();
    void create_compose_cache();

    // The following variables correspond to inputs:
    CompactLattice* inlat_; // Stored input.
    rescore_job_ptr session_;
    BaseFloat acoustic_scale_;
    // models and stuff
    ConstArpaLm* rescore_lm_;
    fst::MapFst<fst::StdArc, LatticeArc, fst::StdToLatticeMapper<BaseFloat> >* lm_fst_;
    fst::VectorFst<fst::StdArc>* std_lm_fst_;
    fst::TableComposeCache<fst::Fst<LatticeArc> >* lm_compose_cache_;

    bool computed_;
    bool success_;
    CompactLattice* outlat_; // Stored output.
};

LatticeRescoreTask::LatticeRescoreTask(
    CompactLattice* lattice,
    rescore_job_ptr session,
    ConstArpaLm* rescore_lm,
    fst::VectorFst<fst::StdArc>* std_lm_fst,
    BaseFloat acoustic_scale)
    :  inlat_(lattice),
       session_(session),
       acoustic_scale_(acoustic_scale),
       rescore_lm_(rescore_lm),
       lm_fst_(NULL),
       std_lm_fst_(std_lm_fst),
       lm_compose_cache_(NULL),
       computed_(false), 
       success_(false),
       outlat_(NULL)  { 
}

/**
*** Reload lm fst
**/
void LatticeRescoreTask::reload_lm_fst() {

    if (lm_fst_) {
        delete lm_fst_;
        lm_fst_ = NULL;
    }
    // mapped_fst is the LM fst interpreted using the LatticeWeight semiring,
    // with all the cost on the first member of the pair (since it's a graph
    // weight).
    int32 num_states_cache = 50000;
    fst::CacheOptions cache_opts(true, num_states_cache);
    fst::StdToLatticeMapper<BaseFloat> mapper;
    lm_fst_ = new fst::MapFst<fst::StdArc, LatticeArc,
        fst::StdToLatticeMapper<BaseFloat> >(*std_lm_fst_, mapper, cache_opts);
}

/**
*** Create table compose cache
**/
void LatticeRescoreTask::create_compose_cache() {
    if (lm_compose_cache_) {
        delete lm_compose_cache_;
        lm_compose_cache_ = NULL;
    }
    // Change the options for TableCompose to match the input
    // (because it's the arcs of the LM FST we want to do lookup
    // on).
    fst::TableComposeOptions compose_opts(fst::TableMatcherOptions(),
                                      true, fst::SEQUENCE_FILTER,
                                      fst::MATCH_INPUT);

    // The following is an optimization for the TableCompose
    // composition: it stores certain tables that enable fast
    // lookup of arcs during composition.
    lm_compose_cache_ = new fst::TableComposeCache<fst::Fst<LatticeArc> >(compose_opts);
}

void LatticeRescoreTask::operator () () {

    reload_lm_fst();
    create_compose_cache();

    outlat_ = new CompactLattice();
    if ( rescore_lattice(inlat_, outlat_)) {
        // We'll write the lattice without acoustic scaling.
        if (acoustic_scale_ != 0.0) {
            fst::ScaleLattice(fst::AcousticLatticeScale(1.0 / acoustic_scale_), outlat_);
        }
        success_ = true;
    } else {
        // report error and use output the same lattice without rescoring          
        delete outlat_; // don't forget to free recently allocated outlat_
        outlat_ = inlat_;
        KALDI_WARN << "Lattice rescoring failed. Outputting the same lattice";
    }

    computed_ = true;

    // Output lattice        
    rescore_message *out = new rescore_message();
    out->body_length(out->max_body_length);        
    obufferstream str(out->body(), out->body_length());
    if (! WriteCompactLattice(str, true, *outlat_) ) {
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
bool LatticeRescoreTask::rescore_lattice(CompactLattice* clat, CompactLattice* result_lat) {

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
  // Could just do, more simply: Compose(lat, lm_fst, &composed_lat);
  // and not have lm_compose_cache at all.
  // The command below is faster, though; it's constant not
  // logarithmic in vocab size.

  TableCompose(tmp_lattice, *(lm_fst_), &composed_lat, lm_compose_cache_);

  Invert(&composed_lat); // make it so word labels are on the input.
  CompactLattice determinized_lat;
  DeterminizeLattice(composed_lat, &determinized_lat);
  fst::ScaleLattice(fst::GraphLatticeScale(-1.0), &determinized_lat);
  if (determinized_lat.Start() == fst::kNoStateId) {
    KALDI_ERR << "Empty lattice (incompatible LM?)";
    return false;
  } else {
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


LatticeRescoreTask::~LatticeRescoreTask() {
    if (!computed_) {    
        KALDI_ERR << "Destructor called without operator (), error in calling code.";
    }

    delete outlat_;   
    delete lm_fst_;
    delete lm_compose_cache_;
}



class rescore_dispatch::impl {


public:

    impl(TaskSequencerConfig sequencer_config, std::string rescore_lm_rspecifier, std::string lm_fst_rspecifier) 
        : sequencer(sequencer_config), 
          acoustic_scale(0)
    {
        // load models and graphs  
        rescore_lm_ = new ConstArpaLm();
        ReadKaldiObject(rescore_lm_rspecifier, rescore_lm_);
        load_lm_fst(lm_fst_rspecifier);
    };
     
    void rescore(const rescore_message& msg, rescore_job_ptr const session) {
        ibufferstream input_stream(msg.body(), msg.body_length());
        CompactLattice *lat = NULL;
        if (ReadCompactLattice(input_stream, true, &lat)) {
            
            // rescore lattice
            // LatticeRescoreTask will take ownership of lat
            LatticeRescoreTask *task =
              new LatticeRescoreTask(lat, session, rescore_lm_, std_lm_fst_, acoustic_scale);
              
            sequencer.Run(task); // takes ownership of "task",
                               // and will delete it when done.

            // write rescored lattice to message
        } else {
            KALDI_ERR << "Failed to read lattice";
            session->close();
        }
    }

private:
    TaskSequencer<LatticeRescoreTask> sequencer;
    BaseFloat acoustic_scale;

    ConstArpaLm* rescore_lm_;
    fst::VectorFst<fst::StdArc> *std_lm_fst_;


    void load_lm_fst(std::string lm_fst_file) {

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

            //delete std_lm_fst;
            delete fst;


         } catch (std::runtime_error& e) {
            KALDI_ERR << "Error loading the FST decoding graph: " << lm_fst_file;
         }
    }
};


rescore_dispatch::rescore_dispatch(TaskSequencerConfig &sequencer_config, std::string rescore_lm_rspecifier, std::string lm_fst_rspecifier) 
:pimpl(new impl(sequencer_config, rescore_lm_rspecifier, lm_fst_rspecifier))
{
}

rescore_dispatch::~rescore_dispatch() {
    delete pimpl;
}

void rescore_dispatch::rescore(const rescore_message& msg, rescore_job_ptr const session) {
    pimpl->rescore(msg, session);
}
