#include "base/kaldi-common.h"
#include "util/parse-options.h"
#include "util/kaldi-thread.h"

#include "rescore_common.hpp"
#include "rescore_message.hpp"

using namespace kaldi;


class RescoreDispatch {

public:

    RescoreDispatch(TaskSequencerConfig &sequencer_config, 
        std::string rescore_lm_rspecifier, 
        std::string lm_fst_rspecifier);

    ~RescoreDispatch();
     
    void rescore(const RescoreMessage& msg, RescoreJobPtr const session);

private:
   class impl;    
   impl* pimpl;
};
