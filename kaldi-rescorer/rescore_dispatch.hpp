#include "base/kaldi-common.h"
#include "util/parse-options.h"
#include "thread/kaldi-task-sequence.h"

#include "rescore_common.hpp"
#include "rescore_message.hpp"

using namespace kaldi;


class rescore_dispatch {

public:

    rescore_dispatch(TaskSequencerConfig &sequencer_config, std::string rescore_lm_rspecifier, std::string lm_fst_rspecifier);

    ~rescore_dispatch();
     
    void rescore(const rescore_message& msg, rescore_job_ptr const session);

private:
   class impl;    
   impl* pimpl;
};
