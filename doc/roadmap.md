# Function TODO:

* [x] memcached 
* [ ] throughput test
  * [x] RPC
  * 1-side
  * +PM
* [ ] p2p
* [x] DCT connect 
* [x] UD connect 
* [x] RPC 
* [ ] coroutine RPC
    * [x] client
    * [ ] server
* [ ] Nested RPC
* [ ] AlNiCo
* ...

# BUGs TODO:

* [x] open_loop时,10Mops无性能
  1. recv cq 太短
  2. cs之间没有同步
* [ ] MP时，POSTPIPE=33时报错
* [ ] kLogNumOfStrides=9时，某些client性能为0
* [ ] RPC size=129时，某些client性能为0
* [x] 当性能统计的区间为奇数时，统计出错
  1. 统计的单位时间为 `x * 1000 / 2000`, 改数组长度2000 为 1000, 单位时间就是 x ns 

# Interface TODO:

* [ ] 同一个CPU core，send buffer在不同qp之间共享
* [ ] 各种queue的长度配置



