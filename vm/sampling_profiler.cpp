#include "master.hpp"

namespace factor {

profiling_sample_count profiling_sample_count::record_counts() volatile {
  atomic::fence();
  profiling_sample_count returned(sample_count, gc_sample_count,
                                  jit_sample_count, foreign_sample_count,
                                  foreign_thread_sample_count);
  atomic::fetch_subtract(&sample_count, returned.sample_count);
  atomic::fetch_subtract(&gc_sample_count, returned.gc_sample_count);
  atomic::fetch_subtract(&jit_sample_count, returned.jit_sample_count);
  atomic::fetch_subtract(&foreign_sample_count, returned.foreign_sample_count);
  atomic::fetch_subtract(&foreign_thread_sample_count,
                         returned.foreign_thread_sample_count);
  return returned;
}

void profiling_sample_count::clear() volatile {
  sample_count = 0;
  gc_sample_count = 0;
  jit_sample_count = 0;
  foreign_sample_count = 0;
  foreign_thread_sample_count = 0;
  atomic::fence();
}

profiling_sample::profiling_sample(profiling_sample_count const& counts, cell thread,
                                   cell callstack_begin, cell callstack_end)
    : counts(counts), thread(thread),
      callstack_begin(callstack_begin),
      callstack_end(callstack_end) { }

void factor_vm::record_sample(bool prolog_p) {
  profiling_sample_count counts = sample_counts.record_counts();
  if (counts.empty()) {
    return;
  }
  /* Appends the callstack, which is just a sequence of quotation or
     word references, to sample_callstacks. */
  cell begin = sample_callstacks.size();

  bool skip_p = prolog_p;
  auto recorder = [&](cell frame_top, cell size, code_block* owner, cell addr) {
    if (skip_p)
      skip_p = false;
    else
      sample_callstacks.push_back(owner->owner);
  };
  iterate_callstack(ctx, recorder);
  cell end = sample_callstacks.size();
  std::reverse(sample_callstacks.begin() + begin, sample_callstacks.end());

  /* Add the sample. */
  cell thread = special_objects[OBJ_CURRENT_THREAD];
  samples.push_back(profiling_sample(counts, thread, begin, end));
}

void factor_vm::set_sampling_profiler(fixnum rate) {
  bool running_p = (atomic::load(&sampling_profiler_p) != 0);
  if (rate > 0 && !running_p)
    start_sampling_profiler(rate);
  else if (rate == 0 && running_p)
    end_sampling_profiler();
}

void factor_vm::start_sampling_profiler(fixnum rate) {
  samples_per_second = rate;
  sample_counts.clear();
  /* Release the memory consumed by collecting samples. */
  samples.clear();
  samples.shrink_to_fit();
  sample_callstacks.clear();
  sample_callstacks.shrink_to_fit();

  samples.reserve(10 * rate);
  sample_callstacks.reserve(100 * rate);
  atomic::store(&sampling_profiler_p, true);
  start_sampling_profiler_timer();
}

void factor_vm::end_sampling_profiler() {
  atomic::store(&sampling_profiler_p, false);
  end_sampling_profiler_timer();
  record_sample(false);
}

void factor_vm::primitive_sampling_profiler() {
  set_sampling_profiler(to_fixnum(ctx->pop()));
}

/* Allocates memory */
void factor_vm::primitive_get_samples() {
  if (atomic::load(&sampling_profiler_p) || samples.empty()) {
    ctx->push(false_object);
  } else {
    data_root<array> samples_array(allot_array(samples.size(), false_object),
                                   this);
    std::vector<profiling_sample>::const_iterator from_iter = samples.begin();
    cell to_i = 0;

    for (; from_iter != samples.end(); ++from_iter, ++to_i) {
      data_root<array> sample(allot_array(7, false_object), this);

      set_array_nth(sample.untagged(), 0,
                    tag_fixnum(from_iter->counts.sample_count));
      set_array_nth(sample.untagged(), 1,
                    tag_fixnum(from_iter->counts.gc_sample_count));
      set_array_nth(sample.untagged(), 2,
                    tag_fixnum(from_iter->counts.jit_sample_count));
      set_array_nth(sample.untagged(), 3,
                    tag_fixnum(from_iter->counts.foreign_sample_count));
      set_array_nth(sample.untagged(), 4,
                    tag_fixnum(from_iter->counts.foreign_thread_sample_count));

      set_array_nth(sample.untagged(), 5, from_iter->thread);

      cell callstack_size =
          from_iter->callstack_end - from_iter->callstack_begin;
      data_root<array> callstack(allot_array(callstack_size, false_object),
                                 this);

      std::vector<cell>::const_iterator callstacks_begin =
                                            sample_callstacks.begin(),
                                        c_from_iter =
                                            callstacks_begin +
                                            from_iter->callstack_begin,
                                        c_from_iter_end =
                                            callstacks_begin +
                                            from_iter->callstack_end;
      cell c_to_i = 0;

      for (; c_from_iter != c_from_iter_end; ++c_from_iter, ++c_to_i)
        set_array_nth(callstack.untagged(), c_to_i, *c_from_iter);

      set_array_nth(sample.untagged(), 6, callstack.value());

      set_array_nth(samples_array.untagged(), to_i, sample.value());
    }
    ctx->push(samples_array.value());
  }
}

}
