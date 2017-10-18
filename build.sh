#!/bin/bash

cd src
KALDI_ROOT=/opt/kaldi make depend
KALDI_ROOT=/opt/kaldi make

cd ../kaldi-rescorer
KALDI_ROOT=/opt/kaldi make


