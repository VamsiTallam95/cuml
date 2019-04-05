/*
 * Copyright (c) 2018, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once
#include "linalg/custom_accum.h"
#include "linalg/gemm.h"
#include "linalg/row_gemm.h"

#include <type_traits>

namespace MLCommon {
namespace Distance {

/**
 * @brief the unexpanded L1 distance matrix calculation
 *  It computes the following equation: cij = op(ai-bj)
 * @tparam InType input data-type (for A and B matrices)
 * @tparam AccType accumulation data-type
 * @tparam OutType output data-type (for C and D matrices)
 * @tparam OutputTile_ output tile size for the thread block
 * @tparam FinalLambda user-defined epilogue lamba
 * @param m number of rows of A and C/D
 * @param n number of columns of B and C/D
 * @param k number of cols of A and rows of B
 * @param pA input matrix
 * @param pB input matrix
 * @param pD output matrix
 * @param fin_op the final element-wise epilogue lambda
 * @param stream cuda stream where to launch work
 * @{
 */
template <typename InType, typename AccType, typename OutType,
          typename OutputTile_, typename FinalLambda>
void l1Impl(int m, int n, int k, InType const *pA, InType const *pB,
            OutType *pD, FinalLambda fin_op, cudaStream_t stream) {
  typedef std::is_same<OutType, bool> is_bool;
  typedef typename std::conditional<is_bool::value, AccType, OutType>::type EffOutType;
  EffOutType* pDCast = reinterpret_cast<EffOutType*>(pD); // Pretend to be EffOutType;

  typedef cutlass::Shape<8, 8, 8> AccumulatorsPerThread_;
  typedef LinAlg::ThreadL1NormAdd<
    AccumulatorsPerThread_, cutlass::Shape<1, 4, 8>, InType, InType, AccType>
    MainLoopFunctor_;
  typedef int Index_;
  typedef LinAlg::CustomGemmConfig<InType, AccType, EffOutType, OutputTile_,
                                   AccumulatorsPerThread_, MainLoopFunctor_>
    GemmConfig_;

  typedef UnexpandedDistanceFragmentMultiplyAdd FragmentMultiplyAdd_;

  typedef UnexpandedDistanceEpilogueFunctor<EffOutType, GemmConfig_,
                                            FragmentMultiplyAdd_>
    EpilogueFunctor_;

  typedef typename std::conditional<is_bool::value,
    BoolEpilogueTraitsHelper<GemmConfig_, EpilogueFunctor_, Index_>,
    cutlass::gemm::GemmEpilogueTraitsHelper<GemmConfig_, EpilogueFunctor_, Index_>>::type
    EpilogueTraitsHelper_;

  typedef typename cutlass::gemm::SimplifiedGemmEpilogueTraits<
    GemmConfig_, EpilogueFunctor_, Index_, EpilogueTraitsHelper_>
    GemmEpilogueTraits_;
  typedef UnexpandedDistanceGemmEpilogue<GemmEpilogueTraits_> GemmEpilogue_;
  typedef typename EpilogueFunctor_::Params EpiParams;

  LinAlg::row_gemm<InType, AccType, EffOutType, OutputTile_,
                   AccumulatorsPerThread_, MainLoopFunctor_, Index_,
                   GemmConfig_, EpilogueFunctor_, GemmEpilogueTraits_,
                   GemmEpilogue_>(
    CUBLAS_OP_N, CUBLAS_OP_T, m, n, k, (EffOutType)1, pA, k, pB, k, (EffOutType)0,
    nullptr, n, pDCast,
    [] HD (EpiParams & p) {
      int err = p.initializeExtra(nullptr, nullptr, false);
      return err;
    },
    fin_op, stream);
}
}
}
