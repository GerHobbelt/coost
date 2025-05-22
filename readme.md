# coost C++17 版本

基于 C++17 的精简版本，移除了一些依赖三方库的功能。

- 编译器: gcc, clang, msvc, **需支持 C++17**；
- 不支持 cmake，需使用 [xmake](https://github.com/xmake-io/xmake) 构建；
- 不支持 vcpkg、conan；
- 不支持动态库；
- 不支持32位系统；


## 功能变更

- **flag**
  - flag 增加 3 种属性: 默认、命令行、隐藏，定义flag可在注释开头加 `@c`, `@h` 指定属性，也可通过 `flag::set_attr` 设置属性；
  - 命令行中用法统一为 `-xx value`，不支持 `-xx=value`;
  - 优化 `-help` 帮助信息显示，coost 内部 flag 与用户定义 flag 分开显示；
  - coost 内部 flag 仅在用户使用了相关功能时才显示，如用户包含了 `co/log.h`，则 `-help` 会显示 coost 日志组件定义的 flag；
  - coost 内部 flag 注释支持中英双语，默认显示中文，可用 `co::mls::set_lang_eng()` 设置为英文；
- **log**
  - 抛弃旧版本打印日志的宏，使用 `log::info`, `log::warn` 等打印日志，如 `log::info("hello ", 23);`；
  - mac、windows不支持 stack trace，linux 可使用 `xmake f --with_backtrace=true` 配置 stack trace；
  - 不支持旧版本的 `TLOG`；
  - **使用 math 库中的 `log()` 函数时，需加上 `::` 限定符**，如 `::log(32)`，以免与 coost `log` 命名空间冲突；
- **benchmark**
  - 优化基准测试定义，形式上与 `unitest` 中定义单元测试保持一致，参考 `test/bm.cc`；
- **协程**
  - 不支持 hook；
  - 协程数量限制：单线程协程数 < 16m, 协程总数 < 2g；
  - 对于同一个 socket，不支持一个协程读，另一个协程同时写；
  - 协程锁重命名为 `co::cutex`；
  - 不支持 channel，无用累赘；
  - 增加 work-stealing 机制；
  - `xmake f --co_debug_log=true` 可打印协程内部的调度日志；
- **终端输出**
  - 增加 `co::cout`, 用法: `co::cout("hello ", 23, co::endl)`;
- **time**
  - 时间相关功能移到命名空间 `time` 中，如 `time::sleep(10);`；
  - 使用 C 标准库的 `time()` 函数时，需加上 `::` 限定符，如 `::time(0);`；
- **其他**
  - 删除 `unlikely` 宏，未来可能与 `C++20` 的 `unlikely` 属性冲突，增加 `if_unlikely` 宏；
  - 删除 HTTP、SSL 相关功能；
  