/* Copyright (c) 2022 Percona LLC and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#include "plugin/audit_log_filter/audit_error_log.h"

#include "plugin/audit_log_filter/audit_filter.h"
#include "plugin/audit_log_filter/audit_keyring.h"
#include "plugin/audit_log_filter/audit_log_filter.h"
#include "plugin/audit_log_filter/audit_log_reader.h"
#include "plugin/audit_log_filter/audit_rule_registry.h"
#include "plugin/audit_log_filter/audit_udf.h"
#include "plugin/audit_log_filter/log_record_formatter.h"
#include "plugin/audit_log_filter/log_writer.h"
#include "plugin/audit_log_filter/log_writer/file_handle.h"
#include "plugin/audit_log_filter/sys_vars.h"

#include <mysql/components/services/mysql_connection_attributes_iterator.h>
#include <mysql/components/services/mysql_current_thread_reader.h>

#include "mysql/plugin.h"
#include "sql/mysqld.h"
#include "sql/sql_class.h"

#include <scope_guard.h>
#include <array>
#include <memory>
#include <variant>

#define PLUGIN_VERSION 0x0100

#ifdef WIN32
#define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PLUGIN_EXPORT extern "C"
#endif

// Needed by mysql/components/services/log_builtins.h
SERVICE_TYPE(log_builtins) *log_bi = nullptr;
SERVICE_TYPE(log_builtins_string) *log_bs = nullptr;

namespace audit_log_filter {
namespace {

AuditLogFilter *audit_log_filter = nullptr;

void deinit_logging_service(SERVICE_TYPE(registry) * reg_srv,
                            SERVICE_TYPE(log_builtins) * *log_bi,
                            SERVICE_TYPE(log_builtins_string) * *log_bs) {
  using log_builtins_t = SERVICE_TYPE_NO_CONST(log_builtins);
  using log_builtins_string_t = SERVICE_TYPE_NO_CONST(log_builtins_string);

  if (*log_bi != nullptr) {
    reg_srv->release(
        reinterpret_cast<my_h_service>(const_cast<log_builtins_t *>(*log_bi)));
  }

  if (*log_bs != nullptr) {
    reg_srv->release(reinterpret_cast<my_h_service>(
        const_cast<log_builtins_string_t *>(*log_bs)));
  }

  *log_bi = nullptr;
  *log_bs = nullptr;
}

bool init_logging_service(SERVICE_TYPE(registry) * reg_srv,
                          SERVICE_TYPE(log_builtins) * *log_bi,
                          SERVICE_TYPE(log_builtins_string) * *log_bs) {
  my_h_service log_srv = nullptr;
  my_h_service log_str_srv = nullptr;

  if (!reg_srv->acquire("log_builtins.mysql_server", &log_srv) &&
      !reg_srv->acquire("log_builtins_string.mysql_server", &log_str_srv)) {
    *log_bi = reinterpret_cast<SERVICE_TYPE(log_builtins) *>(log_srv);
    *log_bs =
        reinterpret_cast<SERVICE_TYPE(log_builtins_string) *>(log_str_srv);
  } else {
    deinit_logging_service(reg_srv, log_bi, log_bs);
    return false;
  }

  return true;
}

bool init_abort_exempt_privilege() {
  my_service<SERVICE_TYPE(dynamic_privilege_register)> reg_priv_srv(
      "dynamic_privilege_register", SysVars::get_comp_registry_srv());

  if (reg_priv_srv.is_valid() && !reg_priv_srv->register_privilege(
                                     STRING_WITH_LEN("AUDIT_ABORT_EXEMPT"))) {
    return true;
  }

  return false;
}

void deinit_abort_exempt_privilege() {
  my_service<SERVICE_TYPE(dynamic_privilege_register)> reg_priv_srv(
      "dynamic_privilege_register", SysVars::get_comp_registry_srv());

  if (reg_priv_srv.is_valid()) {
    reg_priv_srv->unregister_privilege(STRING_WITH_LEN("AUDIT_ABORT_EXEMPT"));
  }
}

}  // namespace

AuditLogFilter *get_audit_log_filter_instance() noexcept {
  return audit_log_filter;
}

/*
 * Audit UDF functions
 */

#define DECLARE_AUDIT_UDF_INIT(NAME)                                      \
  PLUGIN_EXPORT                                                           \
  bool NAME##_udf_init(UDF_INIT *initid, UDF_ARGS *args, char *message) { \
    return audit_log_filter::AuditUdf::NAME##_udf_init(                   \
        audit_log_filter->get_udf(), initid, args, message);              \
  }

#define DECLARE_AUDIT_UDF_FUNC(NAME)                                           \
  PLUGIN_EXPORT                                                                \
  char *NAME##_udf(UDF_INIT *initid, UDF_ARGS *args, char *result,             \
                   unsigned long *length, unsigned char *is_null,              \
                   unsigned char *error) {                                     \
    return audit_log_filter::AuditUdf::NAME##_udf(audit_log_filter->get_udf(), \
                                                  initid, args, result,        \
                                                  length, is_null, error);     \
  }

#define DECLARE_AUDIT_UDF_DEINIT(NAME)                     \
  PLUGIN_EXPORT                                            \
  void NAME##_udf_deinit(UDF_INIT *initid) {               \
    audit_log_filter::AuditUdf::NAME##_udf_deinit(initid); \
  }

#define DECLARE_AUDIT_UDF(NAME) \
  DECLARE_AUDIT_UDF_INIT(NAME)  \
  DECLARE_AUDIT_UDF_FUNC(NAME)  \
  DECLARE_AUDIT_UDF_DEINIT(NAME)

DECLARE_AUDIT_UDF(audit_log_filter_set_filter)
DECLARE_AUDIT_UDF(audit_log_filter_remove_filter)
DECLARE_AUDIT_UDF(audit_log_filter_set_user)
DECLARE_AUDIT_UDF(audit_log_filter_remove_user)
DECLARE_AUDIT_UDF(audit_log_filter_flush)
DECLARE_AUDIT_UDF(audit_log_read)
DECLARE_AUDIT_UDF(audit_log_read_bookmark)
DECLARE_AUDIT_UDF(audit_log_rotate)
DECLARE_AUDIT_UDF(audit_log_encryption_password_get)
DECLARE_AUDIT_UDF(audit_log_encryption_password_set)

#define DECLARE_AUDIT_UDF_INFO(NAME) \
  UdfFuncInfo { #NAME, &NAME##_udf, &NAME##_udf_init, &NAME##_udf_deinit }

static std::array udfs_list{
    DECLARE_AUDIT_UDF_INFO(audit_log_filter_set_filter),
    DECLARE_AUDIT_UDF_INFO(audit_log_filter_remove_filter),
    DECLARE_AUDIT_UDF_INFO(audit_log_filter_set_user),
    DECLARE_AUDIT_UDF_INFO(audit_log_filter_remove_user),
    DECLARE_AUDIT_UDF_INFO(audit_log_filter_flush),
    DECLARE_AUDIT_UDF_INFO(audit_log_read),
    DECLARE_AUDIT_UDF_INFO(audit_log_read_bookmark),
    DECLARE_AUDIT_UDF_INFO(audit_log_rotate),
    DECLARE_AUDIT_UDF_INFO(audit_log_encryption_password_get),
    DECLARE_AUDIT_UDF_INFO(audit_log_encryption_password_set)};

/**
 * @brief Initialize the plugin at server start or plugin installation.
 *
 * @param plugin_info Pointer to plugin info structure
 * @return Initialization status, 0 in case of success or non zero
 *         code otherwise
 */
int audit_log_filter_init(MYSQL_PLUGIN plugin_info [[maybe_unused]]) {
  const auto *comp_registry_srv = SysVars::acquire_comp_registry_srv();

  auto comp_scope_guard = create_scope_guard([&] {
    if (comp_registry_srv != nullptr) {
      deinit_logging_service(comp_registry_srv, &log_bi, &log_bs);
      SysVars::release_comp_registry_srv();
    }
  });

  if (comp_registry_srv == nullptr ||
      !init_logging_service(comp_registry_srv, &log_bi, &log_bs)) {
    return 1;
  }

  LogPluginErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
               "Initializing Audit Event Filter...");

  if (!SysVars::validate()) {
    return 1;
  }

  auto is_keyring_initialized = audit_keyring::check_keyring_initialized();

  if (is_keyring_initialized &&
      !audit_keyring::check_generate_initial_encryption_options()) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                 "Failed to check/generate encryption password");
    return 1;
  }

  SysVars::set_log_encryption_enabled(is_keyring_initialized &&
                                      SysVars::get_encryption_type() !=
                                          AuditLogEncryptionType::None);

  if (!init_abort_exempt_privilege()) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                 "Failed to init AUDIT_ABORT_EXEMPT privilege");
    return 1;
  }

  auto audit_udf = std::make_unique<AuditUdf>();

  if (audit_udf == nullptr) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                 "Failed to create UDFs handler instance");
    return 1;
  }

  if (!audit_udf->init(udfs_list.begin(), udfs_list.end())) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "Failed to init UDFs");
    return 1;
  }

  auto audit_rule_registry = std::make_unique<AuditRuleRegistry>();

  if (audit_rule_registry == nullptr) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                 "Failed to create audit rule registry instance");
    return 1;
  }

  auto formatter = get_log_record_formatter(SysVars::get_format_type());

  if (formatter == nullptr) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                 "Failed to init record formatter");
    return 1;
  }

  auto log_writer = get_log_writer(std::move(formatter));

  if (log_writer == nullptr || !log_writer->init()) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "Failed to init log writer");
    return 1;
  }

  if (!log_writer->open()) {
    char errbuf[MYSYS_STRERROR_SIZE];
    my_strerror(errbuf, sizeof(errbuf), errno);
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Cannot open log writer, error: %s", errbuf);
    return 1;
  }

  auto log_reader = std::make_unique<AuditLogReader>();

  if (log_reader == nullptr) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                 "Failed to create log reader instance");
    return 1;
  }

  log_reader->reset();

  audit_log_filter =
      new AuditLogFilter(std::move(audit_rule_registry), std::move(audit_udf),
                         std::move(log_writer), std::move(log_reader));

  if (audit_log_filter == nullptr || !audit_log_filter->init()) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                 "Failed to init plugin instance");
    return 1;
  }

  // In case of successful initialization,
  // prevent comp_registry_srv from being released by the comp_scope_guard.
  comp_registry_srv = nullptr;

  if (SysVars::get_log_disabled()) {
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                    "Audit Log Filter is disabled. Enable it with "
                    "audit_log_filter_disable = false.");
  } else {
    audit_log_filter->send_audit_start_event();
  }

  return 0;
}

/**
 * @brief Terminate the plugin at server shutdown or plugin deinstallation.
 *
 * @param arg Plugin descriptor pointer
 * @return Plugin deinit status, 0 in case of success or non zero
 *         code otherwise
 */
int audit_log_filter_deinit(void *arg [[maybe_unused]]) {
  if (audit_log_filter == nullptr) {
    return 0;
  }

  audit_log_filter->send_audit_stop_event();
  audit_log_filter->deinit();

  deinit_abort_exempt_privilege();

  LogPluginErr(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
               "Uninstalled Audit Event Filter");

  deinit_logging_service(SysVars::get_comp_registry_srv(), &log_bi, &log_bs);
  SysVars::release_comp_registry_srv();

  delete audit_log_filter;
  audit_log_filter = nullptr;

  return 0;
}

/**
 * @brief Process audit event.
 *
 * @param thd Connection specific THD instance
 * @param event_class Event class
 * @param event Event info
 * @return Event processing status, 0 in case of success or non-zero code
 *         otherwise
 */
int audit_log_notify(MYSQL_THD thd, mysql_event_class_t event_class,
                     const void *event) {
  return audit_log_filter->notify_event(thd, event_class, event);
}

AuditLogFilter::AuditLogFilter(
    std::unique_ptr<AuditRuleRegistry> audit_rules_registry,
    std::unique_ptr<AuditUdf> audit_udf,
    std::unique_ptr<log_writer::LogWriterBase> log_writer,
    std::unique_ptr<AuditLogReader> log_reader)
    : m_audit_rules_registry{std::move(audit_rules_registry)},
      m_audit_udf{std::move(audit_udf)},
      m_log_writer{std::move(log_writer)},
      m_log_reader{std::move(log_reader)},
      m_is_active{true} {}

bool AuditLogFilter::init() noexcept {
  const auto *reg_srv = SysVars::get_comp_registry_srv();

  if (reg_srv->acquire(
          "mysql_thd_security_context",
          reinterpret_cast<my_h_service *>(
              const_cast<SERVICE_TYPE_NO_CONST(mysql_thd_security_context) **>(
                  &m_security_context_srv))) ||
      reg_srv->acquire("mysql_security_context_options",
                       reinterpret_cast<my_h_service *>(
                           const_cast<SERVICE_TYPE_NO_CONST(
                               mysql_security_context_options) **>(
                               &m_security_context_opts_srv))) ||
      reg_srv->acquire(
          "global_grants_check",
          reinterpret_cast<my_h_service *>(
              const_cast<SERVICE_TYPE_NO_CONST(global_grants_check) **>(
                  &m_grants_check_srv)))) {
    return false;
  }

  return m_security_context_srv != nullptr &&
         m_security_context_opts_srv != nullptr &&
         m_grants_check_srv != nullptr;
}

void AuditLogFilter::deinit() noexcept {
  m_is_active = false;
  m_audit_udf->deinit();
  m_log_writer->close();

  const auto *reg_srv = SysVars::get_comp_registry_srv();
  reg_srv->release(reinterpret_cast<my_h_service>(
      const_cast<SERVICE_TYPE_NO_CONST(mysql_thd_security_context) *>(
          m_security_context_srv)));
  reg_srv->release(reinterpret_cast<my_h_service>(
      const_cast<SERVICE_TYPE_NO_CONST(mysql_security_context_options) *>(
          m_security_context_opts_srv)));
  reg_srv->release(reinterpret_cast<my_h_service>(
      const_cast<SERVICE_TYPE_NO_CONST(global_grants_check) *>(
          m_grants_check_srv)));
}

int AuditLogFilter::notify_event(MYSQL_THD thd, mysql_event_class_t event_class,
                                 const void *event) {
  if (SysVars::get_log_disabled() || !m_is_active) {
    return 0;
  }

  SysVars::inc_events_total();

  Security_context_handle ctx;
  std::string user_name;
  std::string user_host;

  if (!get_security_context(thd, &ctx) ||
      !get_connection_user(ctx, user_name, user_host)) {
    return 0;
  }

  // Get connection specific filtering rule
  std::string rule_name;

  if (!m_audit_rules_registry->lookup_rule_name(user_name, user_host,
                                                rule_name)) {
    SysVars::set_session_filter_id(thd, 0);
    return 0;
  }

  auto filter_rule = m_audit_rules_registry->get_rule(rule_name);

  if (filter_rule == nullptr) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Failed to find '%s' filtering rule", rule_name.c_str());
    return 0;
  }

  SysVars::set_session_filter_id(thd, filter_rule->get_filter_id());

  // Get actual event info based on event class
  AuditRecordVariant audit_record = get_audit_record(event_class, event);

  if (std::holds_alternative<AuditRecordUnknown>(audit_record)) {
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                    "Unsupported audit event class with ID %i received",
                    event_class);
    return 0;
  }

  // Apply filtering rule
  AuditAction filter_result =
      AuditEventFilter::apply(filter_rule.get(), audit_record);

  if (filter_result == AuditAction::Skip) {
    SysVars::inc_events_filtered();
    return 0;
  }

  if (filter_result == AuditAction::Block &&
      !check_abort_exempt_privilege(ctx)) {
    auto ev_name = std::visit(
        [](const auto &rec) -> std::string_view { return rec.event_class_name; },
        audit_record);
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "Blocked audit event '%s' with class %i", ev_name.data(),
                    event_class);
    return 1;
  }

  if (event_class == mysql_event_class_t::MYSQL_AUDIT_CONNECTION_CLASS) {
    get_connection_attrs(thd, audit_record);
  }

  m_log_writer->write(audit_record);
  SysVars::inc_events_written();

  return 0;
}

void AuditLogFilter::send_audit_start_event() noexcept {
  my_service<SERVICE_TYPE(mysql_current_thread_reader)> thd_reader_srv(
      "mysql_current_thread_reader", SysVars::get_comp_registry_srv());

  MYSQL_THD thd;

  if (thd_reader_srv->get(&thd)) {
    return;
  }

  if (thd == nullptr) {
    return;
  }

  auto event = audit_filter_event_internal_audit{
      audit_filter_event_subclass_t::AUDIT_FILTER_INTERNAL_AUDIT,
      thd->server_id};
  m_log_writer->write(get_audit_record(
      audit_filter_event_subclass_t::AUDIT_FILTER_INTERNAL_AUDIT, &event));
}

void AuditLogFilter::send_audit_stop_event() noexcept {
  my_service<SERVICE_TYPE(mysql_current_thread_reader)> thd_reader_srv(
      "mysql_current_thread_reader", SysVars::get_comp_registry_srv());

  MYSQL_THD thd;

  if (thd_reader_srv->get(&thd)) {
    return;
  }

  if (thd == nullptr) {
    return;
  }

  auto event = audit_filter_event_internal_noaudit{
      audit_filter_event_subclass_t::AUDIT_FILTER_INTERNAL_NOAUDIT,
      thd->server_id};
  m_log_writer->write(get_audit_record(
      audit_filter_event_subclass_t::AUDIT_FILTER_INTERNAL_NOAUDIT, &event));
}

bool AuditLogFilter::on_audit_rule_flush_requested() noexcept {
  if (!m_is_active) {
    return false;
  }

  const bool is_flushed = m_audit_rules_registry->load();

  DBUG_EXECUTE_IF("audit_log_filter_rotate_after_audit_rules_flush",
                  { m_log_writer->rotate(nullptr); });

  return is_flushed;
}

void AuditLogFilter::on_audit_log_prune_requested() noexcept {
  if (m_is_active) {
    m_log_writer->prune();
  }
}

void AuditLogFilter::on_audit_log_rotate_requested(
    log_writer::FileRotationResult *result) noexcept {
  if (m_is_active) {
    m_log_writer->rotate(result);
    m_log_writer->prune();
  }
}

void AuditLogFilter::on_encryption_password_prune_requested() noexcept {
  if (m_is_active && SysVars::get_password_history_keep_days() > 0 &&
      audit_keyring::check_keyring_initialized()) {
    audit_keyring::prune_encryption_options(
        SysVars::get_password_history_keep_days(),
        log_writer::FileHandle::get_log_names_list(SysVars::get_file_dir(),
                                                   SysVars::get_file_name()));
  }
}

void AuditLogFilter::on_audit_log_rotated() noexcept {
  if (m_is_active) {
    m_log_reader->reset();
  }
}

void AuditLogFilter::get_connection_attrs(MYSQL_THD thd,
                                          AuditRecordVariant &audit_record) {
  my_service<SERVICE_TYPE(mysql_connection_attributes_iterator)> attrs_service(
      "mysql_connection_attributes_iterator", SysVars::get_comp_registry_srv());

  if (!attrs_service.is_valid()) {
    return;
  }

  my_h_connection_attributes_iterator iterator;
  MYSQL_LEX_CSTRING attr_name{nullptr, 0};
  MYSQL_LEX_CSTRING attr_value{nullptr, 0};
  const char *charset_string = nullptr;

  if (attrs_service->init(thd, &iterator)) {
    return;
  }

  auto &info =
      std::visit([](auto &rec) -> ExtendedInfo & { return rec.extended_info; },
                 audit_record);

  info.attrs["connection_attributes"] = {};

  while (!attrs_service->get(thd, &iterator, &attr_name.str, &attr_name.length,
                             &attr_value.str, &attr_value.length,
                             &charset_string)) {
    info.attrs["connection_attributes"].emplace_back(
        std::string{attr_name.str, attr_name.length},
        std::string{attr_value.str, attr_value.length});
  }

  attrs_service->deinit(iterator);
}

bool AuditLogFilter::get_connection_user(Security_context_handle &ctx,
                                         std::string &user_name,
                                         std::string &user_host) noexcept {
  MYSQL_LEX_CSTRING user{"", 0};
  MYSQL_LEX_CSTRING host{"", 0};

  if (m_security_context_opts_srv->get(ctx, "user", &user)) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                 "Can not get user name from security context");
    return false;
  }

  if (m_security_context_opts_srv->get(ctx, "host", &host)) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                 "Can not get user host from security context");
    return false;
  }

  if (user.length == 0 || host.length == 0) {
    return false;
  }

  user_name = user.str;
  user_host = host.str;

  return true;
}

bool AuditLogFilter::check_abort_exempt_privilege(
    Security_context_handle &ctx) noexcept {
  bool has_system_user_grant =
      m_grants_check_srv->has_global_grant(ctx, STRING_WITH_LEN("SYSTEM_USER"));
  bool has_abort_exempt_grant = m_grants_check_srv->has_global_grant(
      ctx, STRING_WITH_LEN("AUDIT_ABORT_EXEMPT"));

  return has_system_user_grant && has_abort_exempt_grant;
}

bool AuditLogFilter::get_security_context(
    MYSQL_THD thd, Security_context_handle *ctx) noexcept {
  if (m_security_context_srv->get(thd, ctx)) {
    LogPluginErr(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "Cannot get security context");
    return false;
  }

  return true;
}

AuditUdf *AuditLogFilter::get_udf() noexcept { return m_audit_udf.get(); }

AuditLogReader *AuditLogFilter::get_log_reader() noexcept {
  return m_log_reader.get();
}

}  // namespace audit_log_filter

static void MY_ATTRIBUTE((constructor)) audit_log_filter_so_init() noexcept {}

/**
 * @brief Plugin type-specific descriptor
 */
static st_mysql_audit audit_log_descriptor = {
    MYSQL_AUDIT_INTERFACE_VERSION,      /* interface version    */
    nullptr,                            /* release_thd function */
    audit_log_filter::audit_log_notify, /* notify function      */
    {                                   /* class mask           */
     static_cast<unsigned long>(MYSQL_AUDIT_GENERAL_ALL),
     static_cast<unsigned long>(MYSQL_AUDIT_CONNECTION_ALL), 0, 0,
     static_cast<unsigned long>(MYSQL_AUDIT_TABLE_ACCESS_ALL),
     static_cast<unsigned long>(MYSQL_AUDIT_GLOBAL_VARIABLE_ALL),
     static_cast<unsigned long>(MYSQL_AUDIT_SERVER_STARTUP_ALL),
     static_cast<unsigned long>(MYSQL_AUDIT_SERVER_SHUTDOWN_ALL),
     static_cast<unsigned long>(MYSQL_AUDIT_COMMAND_ALL),
     static_cast<unsigned long>(MYSQL_AUDIT_QUERY_ALL),
     static_cast<unsigned long>(MYSQL_AUDIT_STORED_PROGRAM_ALL),
     static_cast<unsigned long>(MYSQL_AUDIT_AUTHENTICATION_ALL),
     static_cast<unsigned long>(MYSQL_AUDIT_MESSAGE_ALL)}};

/**
 * @brief Plugin library descriptor
 */
mysql_declare_plugin(audit_log){
    MYSQL_AUDIT_PLUGIN,                   /* type                     */
    &audit_log_descriptor,                /* descriptor               */
    "audit_log_filter",                   /* name                     */
    "Percona LLC and/or its affiliates.", /* author                   */
    "Audit log",                          /* description              */
    PLUGIN_LICENSE_GPL,
    audit_log_filter::audit_log_filter_init, /* init function            */
    nullptr,
    audit_log_filter::audit_log_filter_deinit, /* deinit function          */
    PLUGIN_VERSION,                            /* version                  */
    audit_log_filter::SysVars::get_status_var_defs(),
    audit_log_filter::SysVars::get_sys_var_defs(),
    nullptr,
    0,
} mysql_declare_plugin_end;
