# coroutine example

```c++
int g1[], g2[];

void coroutine (coro_id) {
    local step = coro_id;  
    
    while (1) {
        g1[i] += step;

        yield;
        
        g2[i] = g1[i];
    }
}

void master { // coro_cnt = 8
    for (i from 0 to test_cnt)
        call coroutine(i % coro_cnt)
}
```

* [Boost](./coroutine_boost.cc): 92.0ns - 111.0ns
* [cpp20](./coroutine_cpp20.cc): **6.78 - 8.33ns**

