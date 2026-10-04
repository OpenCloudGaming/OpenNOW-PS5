// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <utility>
namespace opennow::video {
// Caller serializes operations. take() swaps a spare allocation into the ring:
// the returned bytes remain owned by the decoder until its next take(), even
// when the producer fills or clears the queue in the meantime.
class CompressedQueue {
public:
 CompressedQueue()=default;
 CompressedQueue(const CompressedQueue&)=delete;
 CompressedQueue& operator=(const CompressedQueue&)=delete;
 struct Unit {const std::uint8_t* data=nullptr;std::size_t size=0;std::uint64_t received=0;};
 ~CompressedQueue(){close();}
 bool open(unsigned capacity,std::size_t limit,std::size_t padding){
  close();if(!capacity||capacity>buffers_.size()||!limit||padding>SIZE_MAX-limit)return false;
  capacity_=capacity;limit_=limit;padding_=padding;
  for(unsigned i=0;i<capacity_;++i){buffers_[i]=static_cast<std::uint8_t*>(std::malloc(limit_+padding_));if(!buffers_[i]){close();return false;}}
  spare_=static_cast<std::uint8_t*>(std::malloc(limit_+padding_));if(!spare_){close();return false;}return true;
 }
 void close(){for(auto& b:buffers_){std::free(b);b=nullptr;}std::free(spare_);spare_=nullptr;capacity_=0;clear();}
 void clear(){head_=count_=0;}
 unsigned size() const{return count_;}
 bool full() const{return capacity_&&count_==capacity_;}
 bool push(const std::uint8_t* data,std::size_t size,std::uint64_t received){
  if(!capacity_||full()||!data||!size||size>limit_)return false;
  const auto slot=(head_+count_)%capacity_;std::memcpy(buffers_[slot],data,size);
  std::memset(buffers_[slot]+size,0,padding_);sizes_[slot]=size;times_[slot]=received;++count_;return true;
 }
 Unit take(){
  if(!count_)return {};
  const auto slot=head_;std::swap(spare_,buffers_[slot]);head_=(head_+1)%capacity_;--count_;
  return {spare_,sizes_[slot],times_[slot]};
 }
private:
 std::array<std::uint8_t*,8> buffers_{};
 std::array<std::size_t,8> sizes_{};std::array<std::uint64_t,8> times_{};
 std::uint8_t* spare_=nullptr;unsigned capacity_=0,head_=0,count_=0;
 std::size_t limit_=0,padding_=0;
};
}
