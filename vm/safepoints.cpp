#include "master.hpp"

namespace factor {

void factor_vm::enqueue_fep() {
  if (fep_p)
    fatal_error("Low-level debugger interrupted", 0);
  atomic::store(&safepoint_fep_p, true);
  code->set_safepoint_guard(true);
}

void factor_vm::enqueue_samples(cell samples,
                                cell pc,
                                bool foreign_thread_p) {

  if (!atomic::load(&sampling_profiler_p))
    return;
  atomic::fetch_add(&sample_counts.sample_count, samples);

  if (foreign_thread_p)
    atomic::fetch_add(&sample_counts.foreign_thread_sample_count, samples);
  else {
    if (atomic::load(&current_gc_p))
      atomic::fetch_add(&sample_counts.gc_sample_count, samples);
    if (atomic::load(&current_jit_count) > 0)
      atomic::fetch_add(&sample_counts.jit_sample_count, samples);
    if (!code->seg->in_segment_p(pc))
      atomic::fetch_add(&sample_counts.foreign_sample_count, samples);
  }
  code->set_safepoint_guard(true);
}

void factor_vm::handle_safepoint(cell pc) {
  code->set_safepoint_guard(false);
  faulting_p = false;

  if (atomic::load(&safepoint_fep_p)) {
    if (atomic::load(&sampling_profiler_p))
      end_sampling_profiler();
    std::cout << "Interrupted\n";
    factorbug();
    atomic::store(&safepoint_fep_p, false);
  } else if (atomic::load(&sampling_profiler_p)) {
    FACTOR_ASSERT(code->seg->in_segment_p(pc));
    code_block* block = code->code_block_for_address(pc);
    bool prolog_p = block->entry_point() == pc;

    record_sample(prolog_p);
  }
}

}
