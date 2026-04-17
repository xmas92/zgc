Summary
-------
The Java heap configuration of ZGC will automatically adapt to meet the needs of the application, the environment it is running in, as well as other applications running in the same environment. This is initially delivered as an opt-in feature, enabled with a new JVM option: -XX:+ZAdaptive.

Problem
-------
The max heap size (-Xmx) has been treated by ZGC both as an uncompromising hard memory limit that we refuse to exceed by even a single byte. However, it has also had a dual purpose of acting as the main tuning knob to use more heap memory in order to spend less CPU on doing GC.

The GC trade-off between memory, CPU and latency is difficult to reason about and requires a lot of experience and many experiments to get right, in a workload that behaves predictably. In workloads that fluctuate, it becomes increasingly difficult to select an appropriate heap size.

A default max heap size of 25% of the machine or container memory is rarely what users want. As a result, the max heap size is likely the most commonly tuned JVM option that the JVM has to offer.

After running many experiments, a user might, for example, select a relatively large heap size, in order to ensure only a couple of percent of the CPU resources are spent on GC, in order to reduce the CPU induced latency impact of the application. ZGC is then instructed to blindly start a GC just before running out of heap memory so that the concurrent GC cycle will finish up just before the heap memory gets exhausted. If unexpected things happen, there are risks of allocation stalls, where application allocations have to wait for the GC to finish up its concurrent work, before the allocation may be satisfied.

With -XX:SoftMaxHeapSize, it is possible to separate the hard uncompromising memory limit, from the target desired heap size. But this is too advanced for most users, is not taught in school like -Xmx is, and is a static limit which does not fit dynamic workloads.

When the load on the CPUs is lower, perhaps the GC could really have started significantly earlier. But the GC does not know why the heap size was selected, only that it should schedule GC just before exhausting the memory, but not later than that. While the late scheduled GC is running, the application allocation rate might increase unexpectedly, and since the memory limit is uncompromising and hard, there is no way an extra 24 MB will be given to the application to avoid a disastrous allocation stall, where allocating application threads have to wait for the GC to free up garbage. Even though it really would not have been a problem.

Even after getting -Xmx and -XX:SoftMaxHeapSize configured right to accommodate different levels of load and different levels of change in the application behaviour, and perfectly balanced memory with CPU and its impact on P50, P90 and P99 latency, there is still yet another complex trade-off when selecting -Xms. Lower -Xms will imply faster startup, but slower warmup. Higher -Xms will imply slower startup, but faster warmup.

The problem here is that when the GC cannot reason about what a good resource overhead distribution is across CPU and memory, such decisions must be out sourced to the user in the form of JVM options. The users struggle to tune the startup vs warmup vs peak CPU vs peak memory vs peak latency aspects of heap configurations and are likely to not get it right. Yet the GC has to uncompromisingly trust the configuration, causing problems.

After perfectly configuring a JVM with -Xms, -XX:SoftMaxHeapSize and -Xmx to fit the intended application profile, there is an OS layer of problems left. Users that use containers might then have to figure out how large the container should be. Containers come with their own more or less similar hard max, soft max and minimum memory configurations, existing for similar reasons.

With the JVMs and containers perfectly configured, the host machine still needs to configure large pages, which often in enterprise Linux distributions is switched off by default for shared memory (used by the ZGC heap), and switched on by default for anonymous memory (not used by the ZGC heap). Not doing this, can easily have a ~15% performance penalty.

Solution
--------

The high level solution is to teach the JVM about all the various trade-offs and putting the JVM in the driver seat instead. Not only does this alleviate users from having to run lots of experiments and analysis to find out the various configurations, but the JVM can also go beyond manual tuning and deal with dynamic situations in ways that static JVM option numbers simply cannot solve.

The Java heap will tune itself on-the-fly. It will automatically select an appropriate heap size that balances the CPU and memory resources of the machine, any container in use, as well as other applications. It also automatically finds the CPU vs latency trade-offs using queuing theory and moves more CPU work to memory when there is a latency impact. It automatically detects container limits and respects them as part of the control system, but also works well without container limits as multiple JVMs can now adapt to each others demands using the shared communication channel of memory availability.

The heap starts off small (2 MB) by default and memory is committed in the background while running the application, responsive to the application memory demand. While doing so, heap pages are upgraded to use large pages automatically on supported Linux platforms, regardless of system configuration, using madvise MADV_COLLAPSE. Memory is also pre-touched currently with the application threads running. This way, you get both good startup and warmup, and with large pages the warm state is warmer than before, by default.

As memory usage becomes concerning on the machine, GC activity ramps up quadratically, and finally exponentially, before allocations are eventually stalled and OOME is thrown when 95% of the machine memory is used.

Specification
-------------

To ensure a smooth succession, we will initially make automatic heap sizing available, which includes the features described in this section, alongside the current features of ZGC. The -XX:+UseZGC command-line option will run with automatic heap sizing disabled; to enable automatic heap sizing, add the -XX:+ZAdaptive option:
```
$ java -XX:+UseZGC -XX:+ZAdaptive ...
```

In a future release we intend to make automatic heap sizing the default by changing the default value of ZAdaptive to true.

All the features described from here on out are only enabled when automatic heap sizing is enabled using -XX:+ZAdaptive, except for the addition of a new concurrent GC thread which will attempt to upgrade OS pages into large pages, on supported Linux platforms. We feel that this is a feature that users will benefit from, regardless of whether automatic heap sizing is enabled or not, so enabling it in isolation from automatic heap sizing is reasonable. As described in more detail later on, users can opt-out of this feature by using -XX:-UseTransparentHugePages.

The implementation of this work lives in a branch in the ZGC repository right now: https://github.com/openjdk/zgc/tree/zgc_ahs

The main user visible impact of the proposed changes, is the behaviour for heap sizing.

When a user does not select any heap size at all, the "static" max heap size will stop being 25% of the container/machine memory and instead become the entire machine. In fact, even when running in a container, it will become the entire machine memory, so that it can deal with containers changing their memory limits. Previously, we never selected heap sizes above 32 GB with the default heuristics.

The hard memory at which point OOME is thrown, is no longer a static limit at all - it is rather based on memory limits and availability in the container and machine. Therefore, instead of getting OOME at 25% of the machine capacity, users will likely get OOME much later.

In order to affect how heaps are selected between the min heap size (-Xms) and max heap size (-Xmx), a new JVM option may be used: -XX:ZGCIntensity=[0, inf), where 0 has the same effect as disabling automatic heap sizing with -XX:-ZAdaptive. The settings of ZGCIntensity and ZAdaptive are made sure to be coherent under startup, so that if ZAdaptive is on, ZGCIntensity must be a non-zero number, and when ZAdaptive is off, ZGCIntensity must be zero. The main tuning flag is supposed to be ZAdaptive.

If the min heap size (-Xms) and max heap size (-Xmx) are the same value, i.e., -Xms == -Xmx, then automatic heap sizing is always disabled, since the heap can not grow or shrink.

Users that have selected -Xmx but no other heap size, will never end up using more memory. Instead, the impact of this change might be that the JVM uses less memory, unless -Xms is used to push up the minimum heap size. However, when using less memory, there would typically be a good reason for that.

The heap size is exposed in some management APIs, which could notice some subtle differences. The JMX memory pools and memory usage APIs will get a different reported max heap size, which is dynamic rather than static. It is computed by looking at the memory availability of the container and/or machine the JVM is running in. This poses as a small compatibility risk; users that stash away the max heap size and assume it will not be changed, will be surprised.

As for the perf counters that also track heap sizing, the static max heap size is used (the entire machine by default). The reason for this is that this API was not built for changing max heap sizes, and if it can not change dynamically, we better expose the maximum that the process will ever expose throughout its life cycle.

In terms of large pages, this feature relies on using madvise MADV_COLLAPSE from a separate thread to explicitly upgrade heap memory to use transparent huge pages on Linux, when available. Previously, it was a bit more involved. Focusing on ZGC users, where the value of /sys/kernel/mm/transparent_hugepage/shmem_enabled is often "never", users would partially have to change the system settings (requiring super user privileges) from "never" to either "always", or "advise". When using the "advise" option, users would in addition have to use the -XX:+UseTransparentHugePages option, which has no real effect unless the "advise" option is selected for shmem_enabled.

The UseTransparentHugePages JVM option is off by default, due to subtle trade-offs when using transparent huge pages on Linux. In particular, when memory is accessed, there can be long latency hiccups when the kernel needs to perform expensive operations in order to free up contiguous physical memory for large pages. This might be unacceptable for latency sensitive applications. In order to deal with the latency issues, Linux offers a myriad of configuration options to deal with the transparent huge page issues, with subtle trade-offs.

Importantly, with our proposed changes, transparent huge pages will be obtained, by default, despite shmem_enabled being set to "never" and despite UseTransparentHugePages not being turned off. This is a change, and it is motivated by the fact that the trade-offs have been removed, or at least softened to acceptable levels. It is now a concurrent GC thread (per NUMA node) that performs the upgrading to large pages, and any latency impact is absorbed by said GC thread. This way, the worst case latency problems that led to configuration options both for the kernel and the JVM, do not exist any longer. Therefore, this flavour of transparent huge pages is enabled by default. This change will for many users be one of the largest performance improvement out-of-the-box that they have seen.

For users that do not want transparent huge pages, even when the reason is unclear to us, will be able to switch it off with -XX:-UseTransparentHugePages. If the global system setting for shmem_enabled is "always", then the user will nevertheless get transparent huge pages.

With automatic heap sizing, minding feedback cycles in the control system is important for it to work correctly. This has led to some tuning of existing GC heuristics. One notable instance of this, is the soft reference policy. The soft reference policy defines a time interval after which the referent stops being kept alive. Usually, this time interval is based on the amount of "free memory" (the memory that isn't live) in the heap. With most of the machine being free memory in a way, this interacts poorly with the soft reference policy. Even treating the committed heap minus used as free memory is problematic: more soft references means the heap will grow due to GC CPU usage going up to mark through them, which leads to more free memory, which leads to more soft references. This becomes a feedback loop that ends up taking the entire machine in the end.

In order to deal with this, an alternative and a bit more involved soft reference policy is used with automatic heap sizing. This strategy rather looks at the allocation rate in the old generation and how long it will take until the old generation runs out of memory given its current heap size. It multiplies that time by the nth root of the SoftRefLRUPolicyMSPerMB, where n is the free ratio of the heap.

This soft reference policy scales better and behaves well with automatic heap sizing. It is a difference in behaviour, though. The new soft reference policy is strongly tied to automatic heap sizing, and will always be enabled if and only if automatic heap sizing is enabled.

Specification Summary
-------------

Initially deliver automatic heap sizing as an opt-in feature via a new JVM option -XX:(+/-)ZAdaptive, which is off by default, and in a future release make automatic heap sizing the default mode. Concurrently upgrading OS pages to large pages will be enabled by default on supported Linux platforms, and can be opted out of using the existing JVM option -XX:-UseTransparentHugePages.

When automatic heap sizing is enabled, the following new behavior will be observed:

**Heap sizing behavior**

* The default maximum heap size is no longer a fixed fraction (25% of the container/machine, capped at ~32GB), but a dynamic upper bound that can grow up to the full machine memory depending on availability and demand. When -Xmx is explicitly set, it remains a hard upper bound. Automatic heap sizing will not exceed it, but may use less memory depending on system conditions.

* We introduce a new JVM option, -XX:ZGCIntensity=[0, inf), where 0 means do not adapt, practically having the same effect as -XX:-ZAdaptive. Changing -XX:ZGCIntensity is a way to tune towards using more memory or CPU for the GC.

**Observability changes**
* The maximum heap size exposed through java.lang.management.MemoryPoolMXBean, java.lang.management.MemoryUsage and java.lang.Runtime.maxMemory() can now change over time depending on the limits of the environment. According to the specification this behavior should be expected, but is a difference from the numbers reported by ZGC up to this point.

* Perf counters will report the static maximum (all memory on the machine by default, or -Xmx if set explicitly), since they cannot represent a dynamically changing maximum.

**GC policy changes**
* A new soft reference policy for automatic heap sizing is implemented to interact better with a changing heap size. The new strategy looks at the allocation rate in the old generation and how long it will take until the old generation runs out of memory given its current heap size. This avoids feedback loops where increased heap size leads to excessive retention of soft references and unbounded memory growth.

**Performance improvements**
* Transparent huge pages are enabled by default via a concurrent mechanism, mitigating the latency issues traditionally associated with transparent huge pages. This provides improved out-of-the-box performance in supported environments without requiring system configuration.Summary
-------
The Java heap configuration of ZGC will automatically adapt to meet the needs of the application, the environment it is running in, as well as other applications running in the same environment. This is initially delivered as an opt-in feature, enabled with a new JVM option: -XX:+ZAdaptive.

Problem
-------
The max heap size (-Xmx) has been treated by ZGC both as an uncompromising hard memory limit that we refuse to exceed by even a single byte. However, it has also had a dual purpose of acting as the main tuning knob to use more heap memory in order to spend less CPU on doing GC.

The GC trade-off between memory, CPU and latency is difficult to reason about and requires a lot of experience and many experiments to get right, in a workload that behaves predictably. In workloads that fluctuate, it becomes increasingly difficult to select an appropriate heap size.

A default max heap size of 25% of the machine or container memory is rarely what users want. As a result, the max heap size is likely the most commonly tuned JVM option that the JVM has to offer.

After running many experiments, a user might, for example, select a relatively large heap size, in order to ensure only a couple of percent of the CPU resources are spent on GC, in order to reduce the CPU induced latency impact of the application. ZGC is then instructed to blindly start a GC just before running out of heap memory so that the concurrent GC cycle will finish up just before the heap memory gets exhausted. If unexpected things happen, there are risks of allocation stalls, where application allocations have to wait for the GC to finish up its concurrent work, before the allocation may be satisfied.

With -XX:SoftMaxHeapSize, it is possible to separate the hard uncompromising memory limit, from the target desired heap size. But this is too advanced for most users, is not taught in school like -Xmx is, and is a static limit which does not fit dynamic workloads.

When the load on the CPUs is lower, perhaps the GC could really have started significantly earlier. But the GC does not know why the heap size was selected, only that it should schedule GC just before exhausting the memory, but not later than that. While the late scheduled GC is running, the application allocation rate might increase unexpectedly, and since the memory limit is uncompromising and hard, there is no way an extra 24 MB will be given to the application to avoid a disastrous allocation stall, where allocating application threads have to wait for the GC to free up garbage. Even though it really would not have been a problem.

Even after getting -Xmx and -XX:SoftMaxHeapSize configured right to accommodate different levels of load and different levels of change in the application behaviour, and perfectly balanced memory with CPU and its impact on P50, P90 and P99 latency, there is still yet another complex trade-off when selecting -Xms. Lower -Xms will imply faster startup, but slower warmup. Higher -Xms will imply slower startup, but faster warmup.

The problem here is that when the GC cannot reason about what a good resource overhead distribution is across CPU and memory, such decisions must be out sourced to the user in the form of JVM options. The users struggle to tune the startup vs warmup vs peak CPU vs peak memory vs peak latency aspects of heap configurations and are likely to not get it right. Yet the GC has to uncompromisingly trust the configuration, causing problems.

After perfectly configuring a JVM with -Xms, -XX:SoftMaxHeapSize and -Xmx to fit the intended application profile, there is an OS layer of problems left. Users that use containers might then have to figure out how large the container should be. Containers come with their own more or less similar hard max, soft max and minimum memory configurations, existing for similar reasons.

With the JVMs and containers perfectly configured, the host machine still needs to configure large pages, which often in enterprise Linux distributions is switched off by default for shared memory (used by the ZGC heap), and switched on by default for anonymous memory (not used by the ZGC heap). Not doing this, can easily have a ~15% performance penalty.

Solution
--------

The high level solution is to teach the JVM about all the various trade-offs and putting the JVM in the driver seat instead. Not only does this alleviate users from having to run lots of experiments and analysis to find out the various configurations, but the JVM can also go beyond manual tuning and deal with dynamic situations in ways that static JVM option numbers simply cannot solve.

The Java heap will tune itself on-the-fly. It will automatically select an appropriate heap size that balances the CPU and memory resources of the machine, any container in use, as well as other applications. It also automatically finds the CPU vs latency trade-offs using queuing theory and moves more CPU work to memory when there is a latency impact. It automatically detects container limits and respects them as part of the control system, but also works well without container limits as multiple JVMs can now adapt to each others demands using the shared communication channel of memory availability.

The heap starts off small (2 MB) by default and memory is committed in the background while running the application, responsive to the application memory demand. While doing so, heap pages are upgraded to use large pages automatically on supported Linux platforms, regardless of system configuration, using madvise MADV_COLLAPSE. Memory is also pre-touched currently with the application threads running. This way, you get both good startup and warmup, and with large pages the warm state is warmer than before, by default.

As memory usage becomes concerning on the machine, GC activity ramps up quadratically, and finally exponentially, before allocations are eventually stalled and OOME is thrown when 95% of the machine memory is used.

Specification
-------------

To ensure a smooth succession, we will initially make automatic heap sizing available, which includes the features described in this section, alongside the current features of ZGC. The -XX:+UseZGC command-line option will run with automatic heap sizing disabled; to enable automatic heap sizing, add the -XX:+ZAdaptive option:
```
$ java -XX:+UseZGC -XX:+ZAdaptive ...
```

In a future release we intend to make automatic heap sizing the default by changing the default value of ZAdaptive to true.

All the features described from here on out are only enabled when automatic heap sizing is enabled using -XX:+ZAdaptive, except for the addition of a new concurrent GC thread which will attempt to upgrade OS pages into large pages, on supported Linux platforms. We feel that this is a feature that users will benefit from, regardless of whether automatic heap sizing is enabled or not, so enabling it in isolation from automatic heap sizing is reasonable. As described in more detail later on, users can opt-out of this feature by using -XX:-UseTransparentHugePages.

The implementation of this work lives in a branch in the ZGC repository right now: https://github.com/openjdk/zgc/tree/zgc_ahs

The main user visible impact of the proposed changes, is the behaviour for heap sizing.

When a user does not select any heap size at all, the "static" max heap size will stop being 25% of the container/machine memory and instead become the entire machine. In fact, even when running in a container, it will become the entire machine memory, so that it can deal with containers changing their memory limits. Previously, we never selected heap sizes above 32 GB with the default heuristics.

The hard memory at which point OOME is thrown, is no longer a static limit at all - it is rather based on memory limits and availability in the container and machine. Therefore, instead of getting OOME at 25% of the machine capacity, users will likely get OOME much later.

In order to affect how heaps are selected between the min heap size (-Xms) and max heap size (-Xmx), a new JVM option may be used: -XX:ZGCIntensity=[0, inf), where 0 has the same effect as disabling automatic heap sizing with -XX:-ZAdaptive. The settings of ZGCIntensity and ZAdaptive are made sure to be coherent under startup, so that if ZAdaptive is on, ZGCIntensity must be a non-zero number, and when ZAdaptive is off, ZGCIntensity must be zero. The main tuning flag is supposed to be ZAdaptive.

If the min heap size (-Xms) and max heap size (-Xmx) are the same value, i.e., -Xms == -Xmx, then automatic heap sizing is always disabled, since the heap can not grow or shrink.

Users that have selected -Xmx but no other heap size, will never end up using more memory. Instead, the impact of this change might be that the JVM uses less memory, unless -Xms is used to push up the minimum heap size. However, when using less memory, there would typically be a good reason for that.

The heap size is exposed in some management APIs, which could notice some subtle differences. The JMX memory pools and memory usage APIs will get a different reported max heap size, which is dynamic rather than static. It is computed by looking at the memory availability of the container and/or machine the JVM is running in. This poses as a small compatibility risk; users that stash away the max heap size and assume it will not be changed, will be surprised.

As for the perf counters that also track heap sizing, the static max heap size is used (the entire machine by default). The reason for this is that this API was not built for changing max heap sizes, and if it can not change dynamically, we better expose the maximum that the process will ever expose throughout its life cycle.

In terms of large pages, this feature relies on using madvise MADV_COLLAPSE from a separate thread to explicitly upgrade heap memory to use transparent huge pages on Linux, when available. Previously, it was a bit more involved. Focusing on ZGC users, where the value of /sys/kernel/mm/transparent_hugepage/shmem_enabled is often "never", users would partially have to change the system settings (requiring super user privileges) from "never" to either "always", or "advise". When using the "advise" option, users would in addition have to use the -XX:+UseTransparentHugePages option, which has no real effect unless the "advise" option is selected for shmem_enabled.

The UseTransparentHugePages JVM option is off by default, due to subtle trade-offs when using transparent huge pages on Linux. In particular, when memory is accessed, there can be long latency hiccups when the kernel needs to perform expensive operations in order to free up contiguous physical memory for large pages. This might be unacceptable for latency sensitive applications. In order to deal with the latency issues, Linux offers a myriad of configuration options to deal with the transparent huge page issues, with subtle trade-offs.

Importantly, with our proposed changes, transparent huge pages will be obtained, by default, despite shmem_enabled being set to "never" and despite UseTransparentHugePages not being turned off. This is a change, and it is motivated by the fact that the trade-offs have been removed, or at least softened to acceptable levels. It is now a concurrent GC thread (per NUMA node) that performs the upgrading to large pages, and any latency impact is absorbed by said GC thread. This way, the worst case latency problems that led to configuration options both for the kernel and the JVM, do not exist any longer. Therefore, this flavour of transparent huge pages is enabled by default. This change will for many users be one of the largest performance improvement out-of-the-box that they have seen.

For users that do not want transparent huge pages, even when the reason is unclear to us, will be able to switch it off with -XX:-UseTransparentHugePages. If the global system setting for shmem_enabled is "always", then the user will nevertheless get transparent huge pages.

With automatic heap sizing, minding feedback cycles in the control system is important for it to work correctly. This has led to some tuning of existing GC heuristics. One notable instance of this, is the soft reference policy. The soft reference policy defines a time interval after which the referent stops being kept alive. Usually, this time interval is based on the amount of "free memory" (the memory that isn't live) in the heap. With most of the machine being free memory in a way, this interacts poorly with the soft reference policy. Even treating the committed heap minus used as free memory is problematic: more soft references means the heap will grow due to GC CPU usage going up to mark through them, which leads to more free memory, which leads to more soft references. This becomes a feedback loop that ends up taking the entire machine in the end.

In order to deal with this, an alternative and a bit more involved soft reference policy is used with automatic heap sizing. This strategy rather looks at the allocation rate in the old generation and how long it will take until the old generation runs out of memory given its current heap size. It multiplies that time by the nth root of the SoftRefLRUPolicyMSPerMB, where n is the free ratio of the heap.

This soft reference policy scales better and behaves well with automatic heap sizing. It is a difference in behaviour, though. The new soft reference policy is strongly tied to automatic heap sizing, and will always be enabled if and only if automatic heap sizing is enabled.

Specification Summary
-------------

Initially deliver automatic heap sizing as an opt-in feature via a new JVM option -XX:(+/-)ZAdaptive, which is off by default, and in a future release make automatic heap sizing the default mode. Concurrently upgrading OS pages to large pages will be enabled by default on supported Linux platforms, and can be opted out of using the existing JVM option -XX:-UseTransparentHugePages.

When automatic heap sizing is enabled, the following new behavior will be observed:

**Heap sizing behavior**

* The default maximum heap size is no longer a fixed fraction (25% of the container/machine, capped at ~32GB), but a dynamic upper bound that can grow up to the full machine memory depending on availability and demand. When -Xmx is explicitly set, it remains a hard upper bound. Automatic heap sizing will not exceed it, but may use less memory depending on system conditions.

* We introduce a new JVM option, -XX:ZGCIntensity=[0, inf), where 0 means do not adapt, practically having the same effect as -XX:-ZAdaptive. Changing -XX:ZGCIntensity is a way to tune towards using more memory or CPU for the GC.

**Observability changes**
* The maximum heap size exposed through java.lang.management.MemoryPoolMXBean, java.lang.management.MemoryUsage and java.lang.Runtime.maxMemory() can now change over time depending on the limits of the environment. According to the specification this behavior should be expected, but is a difference from the numbers reported by ZGC up to this point.

* Perf counters will report the static maximum (all memory on the machine by default, or -Xmx if set explicitly), since they cannot represent a dynamically changing maximum.

**GC policy changes**
* A new soft reference policy for automatic heap sizing is implemented to interact better with a changing heap size. The new strategy looks at the allocation rate in the old generation and how long it will take until the old generation runs out of memory given its current heap size. This avoids feedback loops where increased heap size leads to excessive retention of soft references and unbounded memory growth.

**Performance improvements**
* Transparent huge pages are enabled by default via a concurrent mechanism, mitigating the latency issues traditionally associated with transparent huge pages. This provides improved out-of-the-box performance in supported environments without requiring system configuration.