
all:

KALDI_ROOT?=/data/build/kaldi-trunk

ifeq ("$(wildcard $(KALDI_ROOT)/src/kaldi.mk)","")
$(error Cannot find Kaldi's makefile $(KALDI_ROOT)/src/kaldi.mk. \
Specify Kaldi's root directory using KALDI_ROOT when issuing make, e.g.: `KALDI_ROOT=/data/build/kaldi-trunk make` )
endif

include $(KALDI_ROOT)/src/kaldi.mk
ifneq ($(KALDI_FLAVOR), dynamic)
$(error Kaldi must compiled with dynamic libraries support. Run configure with --shared flag. )
endif

CXXFLAGS+=-I$(KALDI_ROOT)/src
CXXFLAGS+=-I /usr/lib/boost/include

# Kaldi shared libraries
EXTRA_LDLIBS += -L$(KALDI_ROOT)/src/lib -L$(KALDI_ROOT)/tools/openfst/lib -lkaldi-online2 -lkaldi-lat -lkaldi-decoder -lkaldi-feat -lkaldi-transform \
 -lkaldi-gmm -lkaldi-hmm -lkaldi-chain -lkaldi-fstext \
 -lkaldi-tree -lkaldi-matrix  -lkaldi-util -lkaldi-base -lkaldi-lm -lfstscript -lkaldi-nnet2 -lkaldi-cudamatrix \
 -lkaldi-ivector -lkaldi-nnet3 -lkaldi-rnnlm

# Boost
EXTRA_LDLIBS += -lboost_system -lboost_date_time

# MKL libs required when linked via shared library
ifdef MKLROOT
EXTRA_LDLIBS+=-lmkl_p4n -lmkl_def
endif

# g++ linker
EXTRA_LDLIBS +=  -Wl,--no-as-needed -Wl,-rpath=$(KALDILIBDIR) -lrt -pthread

# target definitions
OBJFILES = rescore_dispatch.o

OBJFILES_unix = rescorer_unix.o
OBJFILES_tcp = rescorer_tcp.o

BINFILES_unix = rescorer_unix
BINFILES_tcp = rescorer_tcp

$(BINFILES_unix): $(OBJFILES) $(OBJFILES_unix)
$(BINFILES_tcp): $(OBJFILES) $(OBJFILES_tcp)

# we only want to link the actually used files,
# otherwise linker complains about multiple mains(), hence OBJFILES_FINAL...
unix: OBJFILES_FINAL=$(OBJFILES) $(OBJFILES_unix)
unix: $(BINFILES_unix)

tcp: OBJFILES_FINAL=$(OBJFILES) $(OBJFILES_tcp)
tcp: $(BINFILES_tcp)

all: OBJFILES_FINAL=$(OBJFILES) $(OBJFILES_unix) $(OBJFILES_tcp)
all: unix tcp

.o:
	$(CXX) -DPIC -o $@ $(EXTRA_LDLIBS) $(LDLIBS) $(LDFLAGS) \
	  $(OBJFILES_FINAL)

clean: 
	-rm -f *.o *.a $(TESTFILES) $(BINFILES_unix) $(BINFILES_tcp)
 

