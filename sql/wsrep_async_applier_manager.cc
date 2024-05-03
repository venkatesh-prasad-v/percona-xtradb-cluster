#include "sql/wsrep_async_applier_manager.h"
#include <algorithm>

#include "sql/rpl_rli_pdb.h"  // Slave_worker
#include "sql/sql_class.h"    // THD

void Wsrep_async_replica_applier_manager::wait(Slave_worker *worker) {

  /* 
    For each entry in the m_applier_info check only workers
    which are operating on same channel.
  */
  std::unique_lock<std::mutex> lock(m_mutex);
  for (auto trx_info : m_applier_info ) {
    std::string channel = trx_info->channel;

    // Check if they are operating on the same channel.
    if (strcmp(worker->get_channel(), channel.c_str()) == 0) {

      // Check the seqeunce number of the transaction. 
      // If the worker's seqno is greater than the seqno obtained from the 
      // trx_info, then wait.
      if (trx_info->sw->sequence_number() < worker->sequence_number()) {
        trx_info->waiting_threads.push_back(worker);
        trx_info->status = enum_trx_status::s_waiting;
        m_cond.wait(lock, [&] { return m_applier_info.front()->sw == worker;});
      }
    }

    // Assert that the worker is the first in the list.
    assert(m_applier_info.front()->sw == worker);

    // Mark as done and clear the waiting threads.
    trx_info->status = enum_trx_status::s_done;
    trx_info->waiting_threads.clear();

    // Remove the trx_info from the list.
    m_applier_info.erase(m_applier_info.begin());

    // Signal the next worker to continue.
    m_cond.notify_all();


  }
}

std::mutex &Wsrep_async_replica_applier_manager::mutex() { return m_mutex; }

std::condition_variable &Wsrep_async_replica_applier_manager::cond() {
  return m_cond;
}

void Wsrep_async_replica_applier_manager::register_trx(
    async_trx_info *trx_info) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_applier_info.push_back(trx_info);
}

void Wsrep_async_replica_applier_manager::unregister_trx(Slave_worker *worker) {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::remove_if(
      m_applier_info.begin(), m_applier_info.end(),
      [&](async_trx_info *trx_info) { return (trx_info->sw == worker); });
}