/* Copyright (c) 2016 Baidu, Inc. All Rights Reserve.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */


#include "Argument.h"

#include <algorithm>

namespace paddle {
static void resizeAndCopy(MatrixPtr& dest, const MatrixPtr& src, bool useGpu,
                          hl_stream_t stream) {
  if (src) {
    if (!dest) {
      dest = src->clone(0, 0, useGpu);
    } else {
      dest->resize(src->getHeight(), src->getWidth());
    }
    dest->copyFrom(*src, stream);
  } else {
    dest.reset();
  }
}

static void resizeAndCopy(IVectorPtr& dest, const IVectorPtr& src, bool useGpu,
                          hl_stream_t stream) {
  if (src) {
    IVector::resizeOrCreate(dest, src->getSize(), useGpu);
    dest->copyFrom(*src, stream);
  } else {
    dest.reset();
  }
}

static void resizeAndCopy(ICpuGpuVectorPtr& dest,
                          const ICpuGpuVectorPtr& src,
                          bool useGpu,
                          hl_stream_t stream) {
  if (src) {
    ICpuGpuVector::resizeOrCreate(dest, src->getSize(), useGpu);
    dest->copyFrom(*src, stream);
  } else {
    dest.reset();
  }
}

static void resizeAndCopy(MatrixPtr& dest, const MatrixPtr& src,
                          int32_t startRow, int32_t copySize, bool useGpu,
                          hl_stream_t stream = HPPL_STREAM_DEFAULT) {
  if (src) {
    CHECK_LE((size_t)startRow + copySize, src->getHeight());

    int height = copySize;
    int width = src->getWidth();
    if (!dest) {
      dest = src->clone(height, width, useGpu);
    } else {
      dest->resize(height, width);
    }
    MatrixPtr submat = src->subMatrix(startRow, copySize);
    dest->copyFrom(*submat, stream);
  } else {
    dest.reset();
  }
}

static void resizeAndCopy(IVectorPtr& dest, const IVectorPtr& src,
                          int32_t startPos, int32_t copySize, bool useGpu,
                          hl_stream_t stream = HPPL_STREAM_DEFAULT) {
  if (src) {
    CHECK_LE((size_t)startPos + copySize, src->getSize());

    int height = copySize;
    IVector::resizeOrCreate(dest, height, useGpu);
    dest->copyFrom(src->getData() + startPos, height, stream);
  } else {
    dest.reset();
  }
}

static void resizeAndCopy(ICpuGpuVectorPtr& dest,
                          const ICpuGpuVectorPtr& src,
                          int32_t startPos,
                          int32_t copySize,
                          bool useGpu,
                          hl_stream_t stream = HPPL_STREAM_DEFAULT) {
  if (src) {
    CHECK_LE((size_t)startPos + copySize, src->getSize());

    ICpuGpuVector::resizeOrCreate(dest, copySize, useGpu);
    dest->copyFrom(*src, startPos, copySize, useGpu, stream);
  } else {
    dest.reset();
  }
}

static void resizeAndCopy(UserDefinedVectorPtr& dest,
                          const UserDefinedVectorPtr& src, bool useGpu,
                          hl_stream_t stream) {
  if (src) {
    CHECK(!useGpu) << "not implemented";
    size_t height = src->size();
    if (!dest) {
      dest = std::make_shared<std::vector<void*>>(height);
    } else {
      dest->resize(height);
    }
    std::copy_n(src->begin(), height, dest->begin());
  } else {
    dest.reset();
  }
}

static void resizeAndCopy(UserDefinedVectorPtr& dest,
                          const UserDefinedVectorPtr& src, int32_t startPos,
                          int32_t copySize, bool useGpu,
                          hl_stream_t stream = HPPL_STREAM_DEFAULT) {
  if (src) {
    CHECK(!useGpu) << "not implemented";
    CHECK_LE((size_t)startPos + copySize, src->size());

    size_t height = copySize;
    if (!dest) {
      dest = std::make_shared<std::vector<void*>>(height);
    } else {
      dest->resize(height);
    }
    std::copy_n(src->begin() + startPos, height, dest->begin());
  } else {
    dest.reset();
  }
}

static void resizeAndCopy(SVectorPtr& dest, const SVectorPtr& src, bool useGpu,
                          hl_stream_t stream) {
  if (src) {
    size_t height = src->size();
    if (!dest) {
      dest = std::make_shared<std::vector<std::string>>(height);
    } else {
      dest->resize(height);
    }
    std::copy_n(src->begin(), height, dest->begin());
  } else {
    dest.reset();
  }
}

static void resizeAndCopy(SVectorPtr& dest, const SVectorPtr& src,
                          int32_t startPos, int32_t copySize, bool useGpu,
                          hl_stream_t stream = HPPL_STREAM_DEFAULT) {
  if (src) {
    CHECK_LE((size_t)startPos + copySize, src->size());
    size_t height = copySize;
    if (!dest) {
      dest = std::make_shared<std::vector<std::string>>(height);
    } else {
      dest->resize(height);
    }
    std::copy_n(src->begin() + startPos, height, dest->begin());
  } else {
    dest.reset();
  }
}

void Argument::resizeAndCopyFrom(const Argument& src, bool useGpu,
                                 hl_stream_t stream) {
  dataId = src.dataId;
  resizeAndCopy(value, src.value, useGpu, stream);
  resizeAndCopy(grad, src.grad, useGpu, stream);
  resizeAndCopy(in, src.in, useGpu, stream);
  resizeAndCopy(ids, src.ids, useGpu, stream);
  resizeAndCopy(sequenceStartPositions, src.sequenceStartPositions,
                false /* useGpu */, stream);
  if (src.hasSubseq()) {
    resizeAndCopy(subSequenceStartPositions,
                  src.subSequenceStartPositions, false /* useGpu */, stream);
  }
  resizeAndCopy(udp, src.udp, useGpu, stream);
  resizeAndCopy(strs, src.strs, useGpu, stream);
}

int32_t Argument::resizeAndCopyFrom(const Argument& src, int32_t startSeq,
                                    int32_t copySize, bool useGpu,
                                    hl_stream_t stream) {
  dataId = src.dataId;

  if (!src.sequenceStartPositions) {
    // non-sequence input, copy samples directly
    int32_t startRow = startSeq;
    resizeAndCopy(in, src.in, startRow, copySize, useGpu, stream);
    resizeAndCopy(value, src.value, startRow, copySize, useGpu, stream);
    resizeAndCopy(grad, src.grad, startRow, copySize, useGpu, stream);
    resizeAndCopy(ids, src.ids, startRow, copySize, useGpu, stream);
    resizeAndCopy(udp, src.udp, startRow, copySize, useGpu, stream);
    resizeAndCopy(strs, src.strs, startRow, copySize, useGpu, stream);
    return copySize;
  } else {
    // sequence input
    const int* sequence = src.sequenceStartPositions->getData(false);
    int32_t startRow = sequence[startSeq];           // sample start from here
    int32_t endRow = sequence[startSeq + copySize];  // sample end
    int32_t copyFeatureSize = endRow - startRow;     // num of samples
    resizeAndCopy(in, src.in, startRow, copyFeatureSize, useGpu, stream);
    resizeAndCopy(value, src.value, startRow, copyFeatureSize, useGpu, stream);
    resizeAndCopy(grad, src.grad, startRow, copyFeatureSize, useGpu, stream);
    resizeAndCopy(ids, src.ids, startRow, copyFeatureSize, useGpu, stream);
    resizeAndCopy(udp, src.udp, startRow, copySize, useGpu, stream);
    resizeAndCopy(sequenceStartPositions, src.sequenceStartPositions,
                  startSeq, copySize + 1, false, stream);
    // modify new sequenceStartPositions
    int* destSequences = sequenceStartPositions->getMutableData(false);
    for (int i = 0; i < copySize + 1; i++) {
      destSequences[i] -= startRow;
    }
    CHECK_EQ(destSequences[0], 0);
    CHECK_EQ(destSequences[copySize], copyFeatureSize);
    if (src.hasSubseq()) {
      // sequence has sub-sequence
      int* subSequence = src.subSequenceStartPositions->getMutableData(false);
      int32_t subStartSeq = 0;
      int32_t subEndSeq = 0;
      int numSubSequences = src.getNumSubSequences();
      for (int i = 0; i < numSubSequences + 1; i++) {
        if (subSequence[i] == startRow) {
          subStartSeq = i;
        } else if (subSequence[i] == endRow) {
          subEndSeq = i;
          break;
        }
      }
      int32_t copySubSize = subEndSeq - subStartSeq;
      resizeAndCopy(subSequenceStartPositions,
                    src.subSequenceStartPositions, subStartSeq,
                    copySubSize + 1, false, stream);
      // modify new subSequenceStartPositions
      int* destSubSequences = subSequenceStartPositions->getMutableData(false);
      for (int i = 0; i < copySubSize + 1; i++) {
        destSubSequences[i] -= startRow;
      }
      CHECK_EQ(destSubSequences[0], 0);
      CHECK_EQ(destSubSequences[copySubSize], copyFeatureSize);
    }
    resizeAndCopy(strs, src.strs, startRow, copySize, useGpu, stream);
    return copyFeatureSize;
  }
}

void Argument::concat(const std::vector<Argument>& args,
                      const std::vector<int>& selectRows,
                      const std::vector<int>& seqStartPos, bool useGpu,
                      hl_stream_t stream, PassType passType) {
  size_t batchSize = selectRows.size();
  auto copyArg = [batchSize, stream](MatrixPtr& dst, MatrixPtr src,
                                     int startRow, int pos, int size,
                                     bool useGpu) {
    if (!src) {
      dst.reset();
      return;
    }
    size_t width = src->getWidth();
    if (!dst) {
      dst = src->clone(batchSize, width, useGpu);
    } else {
      dst->resize(batchSize, width);
    }

    MatrixPtr tmpMatrix = dst->subMatrix(startRow, size);
    tmpMatrix->copyFrom(*src->subMatrix(pos, size), stream);
  };

  auto copyIds = [batchSize, stream](IVectorPtr& dst, const IVectorPtr& src,
                                     int startRow, int pos, int size,
                                     bool useGpu) {
    if (!src) {
      dst.reset();
      return;
    }
    IVector::resizeOrCreate(dst, batchSize, useGpu);
    dst->subVec(startRow, size)->copyFrom(*src->subVec(pos, size), stream);
  };

  auto copyStrs = [batchSize, stream](SVectorPtr& dst, const SVectorPtr& src,
                                      int startRow, int pos, int size,
                                      bool useGpu) {
    if (!src) {
      dst.reset();
      return;
    }
    if (!dst) {
      dst = std::make_shared<std::vector<std::string>>(batchSize);
    } else {
      dst->resize(batchSize);
    }
    std::copy(src->begin() + pos, src->begin() + pos + size,
              dst->begin() + startRow);
  };

  dataId = args[0].dataId;
  CHECK_NE(seqStartPos.size(), 0UL);
  size_t sampleNum = seqStartPos.size() - 1;
  for (size_t i = 0; i < sampleNum; ++i) {
    int startPos = seqStartPos[i];
    int endPos = seqStartPos[i + 1];
    CHECK_GE(args.size(), static_cast<size_t>(endPos - startPos));
    for (int j = startPos; j < endPos; ++j) {
      const Argument& arg = args[j - startPos];
      CHECK_EQ(arg.dataId, dataId) << "Arguments in concat should have"
                                   << " same dataId";
      const int copySize = 1;
      const int rowIdx = selectRows[j];
      copyArg(in, arg.in, j, rowIdx, copySize, useGpu);
      copyArg(value, arg.value, j, rowIdx, copySize, useGpu);
      if (passType != PASS_TEST) {
        copyArg(grad, arg.grad, j, rowIdx, copySize, useGpu);
      }
      copyIds(ids, arg.ids, j, rowIdx, copySize, useGpu);
      copyStrs(strs, arg.strs, j, rowIdx, copySize, useGpu);
    }
  }
  ICpuGpuVector::resizeOrCreate(sequenceStartPositions,
                          seqStartPos.size(), useGpu);
  sequenceStartPositions->copyFrom(seqStartPos.data(),
                                   seqStartPos.size(), useGpu);
}

void Argument::concat(const std::vector<Argument>& args, bool useGpu,
                      hl_stream_t stream, PassType passType) {
  int32_t batchSize = 0;
  int64_t numSequences = 0;
  for (auto& arg : args) {
    batchSize += arg.getBatchSize();
    numSequences += arg.getNumSequences();
  }

  auto copyArg = [batchSize, stream](MatrixPtr& dst, MatrixPtr src,
                                     int startRow, bool useGpu) {
    if (!src) {
      dst.reset();
      return;
    }
    size_t width = src->getWidth();
    if (!dst) {
      dst = src->clone(batchSize, width, useGpu);
    } else {
      dst->resize(batchSize, width);
    }

    MatrixPtr tmpMatrix = dst->subMatrix(startRow, src->getHeight());
    tmpMatrix->copyFrom(*src, stream);
  };

  auto copyIds = [batchSize, stream](IVectorPtr& dst, const IVectorPtr& src,
                                     int startRow, bool useGpu) {
    if (!src) {
      dst.reset();
      return;
    }
    IVector::resizeOrCreate(dst, batchSize, useGpu);
    dst->subVec(startRow, src->getSize())->copyFrom(*src, stream);
  };

  auto copyStrs = [batchSize, stream](SVectorPtr& dst, const SVectorPtr& src,
                                      int startRow, bool useGpu) {
    if (!src) {
      dst.reset();
      return;
    }
    if (!dst) {
      dst = std::make_shared<std::vector<std::string>>(batchSize);
    } else {
      dst->resize(batchSize);
    }
    std::copy(src->begin(), src->end(), dst->begin() + startRow);
  };

  int startRow = 0;
  int startSequences = 0;
  dataId = args[0].dataId;
  for (auto& arg : args) {
    CHECK_EQ(arg.dataId, dataId) << "Arguments in concat should have"
                                 << " same dataId";
    copyArg(in, arg.in, startRow, useGpu);
    copyArg(value, arg.value, startRow, useGpu);
    if (passType != PASS_TEST) copyArg(grad, arg.grad, startRow, useGpu);
    copyIds(ids, arg.ids, startRow, useGpu);
    if (arg.sequenceStartPositions) {
      ICpuGpuVector::resizeOrCreate(sequenceStartPositions,
                                     numSequences + 1,
                                     false);
      const int* src = arg.sequenceStartPositions->getData(false);
      int* dest = sequenceStartPositions->getMutableData(false);
      for (int i = 0; i < arg.getNumSequences() + 1; ++i) {
        dest[i + startSequences] = src[i] + startRow;
      }
      startSequences += arg.getNumSequences();
    }
    copyStrs(strs, arg.strs, startRow, useGpu);
    startRow += arg.getBatchSize();
  }
}

void Argument::splitByDataId(const std::vector<Argument>& argus,
                             std::vector<std::vector<Argument>>* arguGroups) {
  arguGroups->clear();
  int lastDataId = -1;
  for (const auto& argu : argus) {
    if (argu.dataId == -1) {
      // is -1, then create a new group
      arguGroups->emplace_back();
      lastDataId = -1;
    } else if (argu.dataId != lastDataId) {
      // not -1, also not equal to last Argument, then create a new group
      arguGroups->emplace_back();
      lastDataId = argu.dataId;
    } else {
      // not -1, and equal to last Argument, do nothing
    }
    arguGroups->back().push_back(argu);
  }
}

void Argument::getSeqLengthAndStart(
    std::vector<std::tuple<int, int, int, int>>* seqLengthAndStart,
    int* maxSequenceLength) const {
  const int* starts = sequenceStartPositions->getData(false);
  if (hasSubseq()) {
    size_t numSubSequences = getNumSubSequences();
    (*seqLengthAndStart).reserve(numSubSequences);
    const int* subStarts = subSequenceStartPositions->getData(false);
    int seqIndex = 0;
    int subSeqIndex = 0;
    *maxSequenceLength = 0;
    for (size_t i = 0; i < numSubSequences; ++i) {
      if (subStarts[i] == starts[seqIndex]) {
        subSeqIndex = 0;
        (*seqLengthAndStart)
            .push_back(std::make_tuple<int, int, int, int>(
                subStarts[i + 1] - subStarts[i], (int)subStarts[i],
                (int)seqIndex, (int)subSeqIndex));
        ++subSeqIndex;
        ++seqIndex;
      } else if (subStarts[i] < starts[seqIndex]) {
        (*seqLengthAndStart)
            .push_back(std::make_tuple<int, int, int, int>(
                subStarts[i + 1] - subStarts[i], (int)subStarts[i],
                (int)seqIndex - 1, (int)subSeqIndex));
        ++subSeqIndex;
      }
      // maxSequenceLength_ = 1 + max(subSeqIndex) in each Seq.
      if (*maxSequenceLength < std::get<3>((*seqLengthAndStart)[i]))
        *maxSequenceLength = std::get<3>((*seqLengthAndStart)[i]);
    }
    *maxSequenceLength += 1;
  } else {
    size_t numSequences = getNumSequences();
    (*seqLengthAndStart).reserve(numSequences);
    for (size_t i = 0; i < numSequences; ++i) {
      (*seqLengthAndStart)
          .push_back(std::make_tuple<int, int, int, int>(
              starts[i + 1] - starts[i], (int)starts[i], (int)i, (int)i));
    }
    std::sort((*seqLengthAndStart).begin(), (*seqLengthAndStart).end(),
              std::greater<std::tuple<int, int, int, int>>());

    *maxSequenceLength = std::get<0>((*seqLengthAndStart)[0]);
  }
}

void Argument::checkSubset() const {
  if (getNumSequences() > getNumSubSequences()) {
    LOG(FATAL) << "numSubSequences is less than numSequences ("
               << getNumSubSequences() << " vs. " << getNumSequences() << ")";
  }
  const int* start = sequenceStartPositions->getData(false);
  const int* subStart = subSequenceStartPositions->getData(false);
  int seqId = 0;
  int subSeqId = 0;
  while (seqId < getNumSequences() && subSeqId < getNumSubSequences()) {
    if (start[seqId] > subStart[subSeqId]) {
      ++subSeqId;
    } else if (start[seqId] == subStart[subSeqId]) {
      ++subSeqId;
      ++seqId;
    } else {
      LOG(FATAL) << "seqStartPositions is not subset of subSeqStartPositions";
    }
  }
  if (seqId < getNumSequences()) {
    LOG(FATAL) << "seqStartPositions is not subset of subSeqStartPositions";
  }
}

void Argument::degradeSequence(const Argument& input, bool useGpu) {
  CHECK_EQ(input.hasSubseq(), 1UL);
  size_t numSequences = input.getNumSequences();
  size_t numSubSequences = input.getNumSubSequences();
  ICpuGpuVector::resizeOrCreate(sequenceStartPositions,
                                 numSequences + 1,
                                 false);
  int* tgtBuf = sequenceStartPositions->getMutableData(false);
  const int* starts = input.sequenceStartPositions->getData(false);
  const int* subStarts = input.subSequenceStartPositions->getData(false);
  int seqId = 0;
  for (size_t subSeqId = 0; subSeqId < numSubSequences; ++subSeqId) {
    if (subStarts[subSeqId] == starts[seqId]) {
      tgtBuf[seqId] = subSeqId;
      seqId++;
    }
  }
  tgtBuf[numSequences] = numSubSequences;
}

void Argument::subArgFrom(const Argument& input, size_t offset, size_t height,
                          size_t width, bool useGpu, bool trans, bool seqFlag,
                          size_t seqStart, size_t seqSize) {
  value = Matrix::create(input.value->getData() + offset, height, width, trans,
                         useGpu);
  if (input.grad) {
    grad = Matrix::create(input.grad->getData() + offset, height, width, trans,
                          useGpu);
  }
  if (seqFlag) {
    sequenceStartPositions = std::make_shared<ICpuGpuVector>(
        *(input.sequenceStartPositions),
        seqStart, seqSize);
  }
}

}  // namespace paddle
