
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
EXTRA_LDLIBS += -L$(KALDI_ROOT)/tools/openfst/lib -lkaldi-online2 -lkaldi-lat -lkaldi-decoder -lkaldi-feat -lkaldi-transform \
 -lkaldi-gmm -lkaldi-hmm \
 -lkaldi-tree -lkaldi-matrix  -lkaldi-util -lkaldi-base -lkaldi-lm -lfstscript -lkaldi-nnet2 -lkaldi-cudamatrix \
 -lkaldi-ivector

# Boost
EXTRA_LDLIBS += -lboost_system 

OBJFILES = rescorer_unix.o rescore_dispatch.o

BINFILES = rescorer_unix

all: $(BINFILES)

# MKL libs required when linked via shared library
ifdef MKLROOT
EXTRA_LDLIBS+=-lmkl_p4n -lmkl_def
endif

# Library so name and rpath

# g++ linker
EXTRA_LDLIBS +=  -Wl,--no-as-needed -Wl,-rpath=$(KALDILIBDIR) -lrt -pthread



$(BINFILES): $(OBJFILES)

.o:
	$(CXX) -DPIC -o $@ -L$(KALDILIBDIR) $(EXTRA_LDLIBS) $(LDLIBS) $(LDFLAGS) \
	  $(OBJFILES)

clean: 
	-rm -f *.o *.a $(TESTFILES) $(BINFILES)
 

