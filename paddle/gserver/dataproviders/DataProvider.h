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


#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <iostream>
#include <fstream>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "paddle/utils/Logging.h"
#include "paddle/utils/Queue.h"
#include "paddle/utils/Locks.h"
#include "paddle/utils/ThreadLocal.h"
#include "paddle/utils/TypeDefs.h"
#include "paddle/math/Matrix.h"
#include "paddle/math/SparseMatrix.h"
#include "paddle/utils/Util.h"
#include "paddle/math/Vector.h"
#include "DataConfig.pb.h"
#include "paddle/utils/ClassRegistrar.h"
#include "paddle/parameter/Argument.h"

namespace paddle {

/**
 * @brief Macro for registering a data provider.
 */
#define REGISTER_DATA_PROVIDER(__type_name, __class_name)               \
  static InitFunction __reg_type_##__type_name([]() {                   \
    DataProvider::registrar_.registerClass<__class_name>(#__type_name); \
  })

class DataBatch;
class BufferBatch;
typedef std::shared_ptr<DataBatch> DataBatchPtr;
typedef std::shared_ptr<BufferBatch> BufferBatchPtr;

class DataBatch {
public:
  DataBatch() : size_(0) { data_.clear(); }

  int64_t getSize() const { return size_; }

  int64_t getNumSequences() const {
    if (data_.empty()) return size_;
    return data_[0].sequenceStartPositions
               ? data_[0].sequenceStartPositions->getSize() - 1
               : size_;
  }

  void setSize(int64_t size) { size_ = size; }

  int64_t getNumStreams() const { return data_.size(); }

  const Argument& getStream(int i) const { return data_[i]; }

  std::vector<Argument>& getStreams() { return data_; }

  std::vector<Argument> getStreams() const { return data_; }

  void clear() {
    data_.clear();
    size_ = 0;
  }

  /**
   * The order in which each data stream is appended must match the order
   * specified in stream_names of DataConfig. The stream_names can be obtained
   * using DataProvider::getStreamNames().
   */
  void appendData(MatrixPtr data) {
    Argument argu;
    argu.value = data;
    data_.push_back(argu);
  }

  /**
   * The order in which each data stream is appended must match the order
   * specified in stream_names of DataConfig. The stream_names can be obtained
   * using DataProvider::getStreamNames().
   */
  void appendData(const MatrixPtr& data,
                  const ICpuGpuVectorPtr& sequenceStartPositions) {
    Argument argu;
    argu.value = data;
    argu.sequenceStartPositions = sequenceStartPositions;
    data_.push_back(argu);
  }

  void appendLabel(IVectorPtr label, MatrixPtr value = nullptr) {
    Argument argu;
    argu.ids = label;
    argu.value = value;
    data_.push_back(argu);
  }

  void appendUserDefinedPtr(UserDefinedVectorPtr ptr) {
    Argument argu;
    argu.udp = ptr;
    data_.push_back(argu);
  }

  /*
   * argus: DataBatch.getStreams()
   * size: DataBatch.getSize()
   * dataId: sub dataprovider id (in MultiDataProvider)
   */
  void appendArguments(const std::vector<Argument>& argus, int size,
                       int dataId) {
    size_ += size;
    for (const auto& argu : argus) {
      data_.push_back(argu);
      data_.back().dataId = dataId;
    }
  }

protected:
  int64_t size_;
  std::vector<Argument> data_;
};

class BufferBatch {
public:
  BufferBatch() {
    hlStream_ = HPPL_STREAM_DEFAULT;
    hlEvent_ = NULL;
    batchData_ = NULL;
  }
  ~BufferBatch() {
    if (hlEvent_) {
      hl_destroy_event(hlEvent_);
      hlEvent_ = NULL;
    }
    if (batchData_) {
      delete batchData_;
      batchData_ = NULL;
    }
  }

  void setDataBatch(DataBatch* batchData) { batchData_ = batchData; }
  DataBatch* getDataBatch() { return batchData_; }

  void setCuStream(hl_stream_t stream) { hlStream_ = stream; }
  hl_stream_t getCuStream() const { return hlStream_; }

  void setCuEvent(hl_event_t event) { hlEvent_ = event; }

  hl_event_t getCuEvent() const { return hlEvent_; }

  void createCuEvent() {
    if (!hlEvent_) {
      hlStream_ = HPPL_STREAM_1;
      hl_create_event(&hlEvent_);
    }
  }

  void syncEvent() {
    if (hlEvent_) {
      hl_stream_wait_event(hlStream_, hlEvent_);
    }
  }

  void swap(BufferBatch* bufBatch);
  void clone(DataBatch* srcBatch, bool useGpu);

protected:
  DataBatch* batchData_;
  hl_stream_t hlStream_;
  hl_event_t hlEvent_;
};

class DataProvider;
typedef std::shared_ptr<DataProvider> DataProviderPtr;

typedef Queue<BufferBatch*> BufferBatchQueue;

class DoubleBuffer {
public:
  DoubleBuffer(DataProvider* dataPool, bool useGpu, int64_t batchSize = 0);
  virtual ~DoubleBuffer();
  void removeOneBatch(DataBatch* dataBatch);

  void setBatchSize(int64_t newBatchSize) { batchSize_ = newBatchSize; }

  int64_t getBatchSize() { return batchSize_; }

  void startAsyncLoad();
  void finishAsyncLoad() {
    stopping_ = true;
    taskReadySem_.post();
    asyncLoader_->join();
  }

  void setPending(bool pending) { pending_ = pending; }

protected:
  virtual void asyncLoadBatch();
  void insertOneBatch(DataBatch* batch);

  DataProvider* dataPool_;
  bool useGpu_;
  int32_t batchSize_;
  ThreadLocal<BufferBatchPtr> usingBatch_;
  BufferBatchQueue* dataQueue_;
  BufferBatchQueue* bufferQueue_;
  std::unique_ptr<std::thread> asyncLoader_;
  Semaphore taskReadySem_;
  bool stopping_;
  bool pending_;
};

/**
 * DataProvider supplies data for training
 * It can supplies multiple streams of data.
 * For typical supervised training, there are two streams:
 * one is for input, one is for label.
 */
class DataProvider {
public:
  static ClassRegistrar<DataProvider, DataConfig, bool> registrar_;
  static DataProvider* create(const DataConfig& config,
                              bool useGpu = FLAGS_use_gpu);

  DataProvider(const DataConfig& config, bool useGpu)
      : config_(config),
        skipShuffle_(false),
        usageRatio_(config.usage_ratio()),
        useGpu_(useGpu) {
    if (config_.async_load_data()) {
      initAsyncLoader();
    }
  }
  virtual ~DataProvider() {}

  const DataConfig& getConfig() const { return config_; }

  void setSkipShuffle() { skipShuffle_ = true; }
  int64_t getNextBatch(int64_t size, DataBatch* batch);

  /**
   * Shuffle the data set
   */
  virtual void shuffle() = 0;

  /**
   * reset() must be called before any calls to getNextBatch()
   * reset all the value of index
   * IMPORTANT: subclass reset() should always call the base class reset()
   * at the end of the function
   */
  virtual void reset() {
    if (doubleBuffer_ != nullptr) {
      LOG(INFO) << "the double-buffer is starting ...";
      doubleBuffer_->startAsyncLoad();
    }
  }

  /**
   * return the number of training samples in the data set.
   * return -1 to indicate unlimited number of samples.
   */
  virtual int64_t getSize() = 0;

  virtual int64_t getNextBatchInternal(int64_t size, DataBatch* batch) = 0;

protected:
  DataConfig config_;
  bool skipShuffle_;
  float usageRatio_;
  bool useGpu_;
  std::unique_ptr<DoubleBuffer> doubleBuffer_;
  ThreadLocal<std::vector<MatrixPtr>> constantSlots_;

  int64_t getNextBatchFromBuffer(int64_t size, DataBatch* batch);

  void initAsyncLoader();
};

/**
 * A data provider which does nothing. It only serves as providing
 * necessary configurations such as stream_names
 */
class DummyDataProvider : public DataProvider {
public:
  DummyDataProvider(const DataConfig& config, bool useGpu)
      : DataProvider(config, useGpu) {}
  virtual void shuffle() {}
  virtual void reset() { DataProvider::reset(); }
  virtual int64_t getSize() { return 0; }
  virtual int64_t getNextBatchInternal(int64_t size, DataBatch* batch) {
    (void)size;
    (void)batch;
    return 0;
  }
};

// Data provider for one input and one integer label
class SimpleDataProviderBase : public DataProvider {
protected:
  int64_t sampleDim_;       // sample feature dimension
  int64_t bufferCapacity_;  // the number of samples
  int64_t sampleNumInBuf_;
  int64_t nextItemIndex_;  // next item to read in buffer
  bool withInfo_;          // some user defined info for validation

  // data buffer: bufferCapacity_ * nDataDim_
  CpuMatrixPtr hInputDataBuf_;

  // label buffer:bufferCapacity_ * 1
  CpuIVectorPtr hInputLabelBuf_;

  // info buffer:bufferCapacity_ * 1
  CpuIVectorPtr hInputInfoBuf_;

  ThreadLocal<MatrixPtr> dataBatch_;
  ThreadLocal<IVectorPtr> labelBatch_;
  ThreadLocal<IVectorPtr> infoBatch_;

  RWLock lock_;

public:
  SimpleDataProviderBase(const DataConfig& config, bool useGpu, bool withInfo);
  ~SimpleDataProviderBase() {}

  void shuffle();

  virtual void reset();

  virtual int64_t getSize();

  virtual int64_t getNextBatchInternal(int64_t size, DataBatch* batch);

  // return the number of samples in the buffer
  int64_t fillBuffer();

protected:
  /**
   * @brief Fill at most size samples into data and label.
   *
   * Each input is stored in contiguous memory locations in data.
   *
   * data[n * sampleDim_] .. data[n * sampleDim_ + sampleDim_ - 1] is for
   * the input of the n-th sample.
   *
   * label[n] is the label for the n-th sample.
   */
  virtual int64_t fillBufferImp(real* data, int* label, int* info,
                                int64_t size) = 0;
};

class SimpleDataProvider : public SimpleDataProviderBase {
public:
  SimpleDataProvider(const DataConfig& config, bool useGpu);
  ~SimpleDataProvider();
  virtual void reset();

protected:
  void loadData(const std::string& fileName);
  void loadDataFile(const std::string& fileName);
  virtual int64_t fillBufferImp(real* data, int* label, int* info,
                                int64_t size);

protected:
  size_t currentSampleIndex_;
  std::vector<int> labels_;
  std::vector<real> data_;
};

}  // namespace paddle
