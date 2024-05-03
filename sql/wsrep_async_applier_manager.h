#ifndef WSREP_ASYNC_APPLIER_MANAGER_H
#define WSREP_ASYNC_APPLIER_MANAGER_H
#include <condition_variable>
#include <map>
#include <mutex>
#include <vector>

// #include "sql/rpl_rli_pdb.h" // Slave_worker
// #include "sql/sql_class.h"   // THD
class THD;
class Slave_worker;

enum class enum_trx_type { NOT_SET, DDL, DML };

enum class enum_trx_status {
  s_not_started = 0,
  s_executing,
  s_waiting,
  s_done
};

struct async_trx_info {
  Slave_worker *sw;
  enum_trx_type type;
  enum_trx_status status;
  std::string channel;
  std::vector<Slave_worker *> waiting_threads;
};

using Applier_info = std::vector<async_trx_info *>;

class Wsrep_async_replica_applier_manager {
 public:
  Wsrep_async_replica_applier_manager() {}

  void register_trx(async_trx_info *trx_info);
  void unregister_trx(Slave_worker *worker);
  void wait(Slave_worker *worker);

  std::mutex &mutex();
  std::condition_variable &cond();

 private:
  Applier_info m_applier_info;
  std::mutex m_mutex;
  std::condition_variable m_cond;
};
#endif /* WSREP_ASYNC_APPLIER_MANAGER_H */
