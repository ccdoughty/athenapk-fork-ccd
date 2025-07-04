#!/bin/bash

set -e
export PATH="/mpcdf/soft/RHEL_9/packages/x86_64/rocm/6.3.3/bin:$PATH"
export CXX=hipcc
export CC=clang

export APKDIR="athenapk-fork-ccd"

#rm -rf athenapk-fork-ccd
#git clone https://github.com/ccdoughty/athenapk-fork-ccd.git athenapk-fork-ccd

cd $APKDIR

# get submodules (mainly Kokkos and Parthenon)
#git submodule init
#git submodule update

#cd external/Kokkos
#git checkout develop  # get fixes for amd architecture?
#cd ../../

#git checkout pressure_diseq
#single-cloud-tracking

module purge
module load clang/18 cuda/11.4 gcc/11 openmpi/4 hdf5-mpi/1.12.1
#module load clang/18 gcc/14 rocm/6.3 openmpi_gpu/5.0 cmake/3.30 hdf5-mpi/1.14.1 python-waterboa/2024.06#
module list

#git checkout single-cloud-tracking
# for MPCDF Viper GPU:
#cmake -S. -Bbuild-gpu -DKokkos_ARCH_ZEN4=ON -DKokkos_ENABLE_HIP=ON -DKokkos_ARCH_AMD_GFX942=ON -D CMAKE_CXX_COMPILER=hipcc

# for MPCDF Raven GPU:
cmake -S. -Bbuild-gpu -DKokkos_ARCH_SKX=ON -DKokkos_ENABLE_CUDA=ON -DKokkos_ARCH_AMPERE80=ON  -DCMAKE_CXX_COMPILER=${PWD}/external/Kokkos/bin/nvcc_wrapper -DPARTHENON_ENABLE_PYTHON_MODULE_CHECK=OFF

cd build-gpu
make -j 8
