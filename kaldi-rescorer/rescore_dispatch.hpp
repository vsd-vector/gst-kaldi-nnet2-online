#include "base/kaldi-common.h"
#include "util/parse-options.h"
#include "util/kaldi-thread.h"
#include "rnnlm/rnnlm-lattice-rescoring.h"


#include "rescore_common.hpp"
#include "rescore_message.hpp"

using namespace kaldi;

class RescoreDispatch {
public:
    RescoreDispatch(TaskSequencerConfig &sequencer_config,
        const std::string &rescore_mode,
        const std::string &lm_fst_rspecifier,
        const std::string &carpa_rspecifier,
        const std::string &rnnlm_dir,
        kaldi::int32 max_ngram_order,
        bool do_carpa_rescore,
        bool do_rnnlm_rescore
        );

    ~RescoreDispatch();

    void rescore(const RescoreMessage& msg, const RescoreJobPtr session);

private:
   class impl;
   impl* pimpl;
};
