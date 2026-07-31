#include <dbus/dbus.h>
#include <vector>
#include <string>

#include "utils/log.h"
#include "OMXControl.h"
#include "KeyConfig.h"
#include "DbusCommandSearch.h"

static DBusConnection *bus = nullptr;

OMXControl::~OMXControl()
{
  DbusDisconnect();
}

bool OMXControl::Connect(const char *dbus_name)
{
  if(DbusConnect(dbus_name))
  {
    CLogLog(LOGDEBUG, "DBus connection succeeded");
    dbus_threads_init_default();
    return true;
  }
  else
  {
    CLogLog(LOGWARNING, "DBus connection failed");
    DbusDisconnect();
    return false;
  }
}

void OMXControl::Dispatch()
{
  if (bus)
    dbus_connection_read_write(bus, 0);
}


OMXControl::operator bool() const
{
  return bus;
}

bool OMXControl::DbusConnect(const char *dbus_name)
{
  if(bus != nullptr)
    throw "Only one dbus connection can exist";

  DBusError error;

  dbus_error_init(&error);
  if (!(bus = dbus_bus_get_private(DBUS_BUS_SESSION, &error)))
  {
    CLogLog(LOGWARNING, "dbus_bus_get_private(): %s", error.message);
      goto fail;
  }

  dbus_connection_set_exit_on_disconnect(bus, FALSE);

  if (dbus_bus_request_name(bus, dbus_name, DBUS_NAME_FLAG_DO_NOT_QUEUE, &error)
      != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
  {
    if (dbus_error_is_set(&error))
    {
      CLogLog(LOGWARNING, "dbus_bus_request_name(): %s", error.message);
      goto fail;
    }

    CLogLog(LOGWARNING, "Failed to acquire D-Bus name '%s'", dbus_name);
    goto fail;
  }

  return true;

fail:
  if(dbus_error_is_set(&error))
    dbus_error_free(&error);

  if (bus)
  {
    dbus_connection_close(bus);
    dbus_connection_unref(bus);
    bus = nullptr;
  }

  return false;
}

void OMXControl::DbusDisconnect()
{
  if (bus)
  {
    dbus_connection_close(bus);
    dbus_connection_unref(bus);
    bus = nullptr;
  }
}

enum ControlFlow OMXControl::GetEvent()
{
  if (!bus)
    return CONTINUE;

  Dispatch();
  DBusMessage *m = dbus_connection_pop_message(bus);
  if (m == nullptr)
    return CONTINUE;

  CLogLog(LOGDEBUG, "Popped message member: %s interface: %s type: %d path: %s", dbus_message_get_member(m), dbus_message_get_interface(m), dbus_message_get_type(m), dbus_message_get_path(m) );

  const char *method = dbus_message_get_member(m);
  if (method == nullptr)
    return CONTINUE;

  enum Action action = dbus_find_method(method);
  bool expects_response = !dbus_message_get_no_reply(m);

  DMessage message(m, expects_response);
  return handle_event(action, &message);
}


DMessage::DMessage(DBusMessage *message, bool res)
{
  m = message;
  needs_response = res;
}

DMessage::~DMessage()
{
  if(needs_response)
  {
    DBusMessage *reply = dbus_message_new_method_return(m);

    if(reply)
    {
      dbus_connection_send(bus, reply, nullptr);
      dbus_message_unref(reply);
    }
  }

  if(m_args)
    delete m_args;

  dbus_message_unref(m);
}

bool DMessage::GetArg(int type, void *value)
{
  if(!m_args)
  {
    m_args = new DBusMessageIter;
    dbus_message_iter_init(m, m_args);
  }

  int element_type = dbus_message_iter_get_arg_type(m_args);

  if(element_type == type)
  {
    dbus_message_iter_get_basic(m_args, value);
    dbus_message_iter_next(m_args);
    return true;
  }
  else if(element_type == DBUS_TYPE_VARIANT)
  {
    DBusMessageIter variant;
    dbus_message_iter_recurse(m_args, &variant);
    if(dbus_message_iter_get_arg_type(&variant) == type)
    {
      dbus_message_iter_get_basic(&variant, value);
    }
    dbus_message_iter_next(m_args);
    return true;
  }
  else
  {
    return false;
  }
}

bool DMessage::GetArgInt(int *value)
{
  return GetArg(DBUS_TYPE_INT32, value);
}

bool DMessage::GetArgInt64(int64_t *value)
{
  return GetArg(DBUS_TYPE_INT64, value);
}

bool DMessage::GetArgDouble(double *value)
{
  return GetArg(DBUS_TYPE_DOUBLE, value);
}

bool DMessage::GetArgString(std::string &s)
{
  const char *value;
  bool r = GetArg(DBUS_TYPE_STRING, &value);
  if(r)
    s.assign(value);
  return r;
}

bool DMessage::IgnoreArg()
{
  if(!m_args)
  {
    m_args = new DBusMessageIter;
    dbus_message_iter_init(m, m_args);
  }

  return dbus_message_iter_next(m_args);
}

void DMessage::RespondError(const char *name, const char *msg)
{
  DBusMessage *reply = dbus_message_new_error(m, name, msg);
  if(!reply)
    throw "memory error";

  dbus_connection_send(bus, reply, nullptr);
  dbus_message_unref(reply);
  needs_response = false;
}

void DMessage::RespondUnknownProperty()
{
  RespondError(DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property");
}


void DMessage::RespondUnknownMethod()
{
  RespondError(DBUS_ERROR_UNKNOWN_METHOD, "Unknown method");
}

void DMessage::RespondInvalidArgs()
{
  RespondError(DBUS_ERROR_INVALID_ARGS, "Invalid arguments");
}

void DMessage::RespondInt64(int64_t value)
{
  Respond(DBUS_TYPE_INT64, &value);
}

void DMessage::RespondDouble(double value)
{
  Respond(DBUS_TYPE_DOUBLE, &value);
}

void DMessage::RespondBool(bool value)
{
  int t = value ? 1 : 0;
  Respond(DBUS_TYPE_BOOLEAN, &t);
}

void DMessage::RespondString(const std::string &s)
{
  const char *value = s.c_str();
  Respond(DBUS_TYPE_STRING, &value);
}

void DMessage::Respond(int type, void *value)
{
  DBusMessage *reply = dbus_message_new_method_return(m);

  if (!reply)
    throw "Memory error";

  dbus_message_append_args(reply, type, value, DBUS_TYPE_INVALID);
  dbus_connection_send(bus, reply, nullptr);

  dbus_message_unref(reply);

  needs_response = false;
}

void DMessage::RespondArray(const std::vector<std::string> &list)
{
  const char *char_list[list.size()];

  for(uint i = 0; i < list.size(); i++)
    char_list[i] = &list[i][0];

  RespondArray(char_list, list.size());
}

void DMessage::RespondArray(const char *array[], int size)
{
  DBusMessage *reply = dbus_message_new_method_return(m);

  if (!reply)
    throw "Memory error";

  dbus_message_append_args(reply, DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &array, size, DBUS_TYPE_INVALID);
  dbus_connection_send(bus, reply, nullptr);
  dbus_message_unref(reply);

  needs_response = false;
}

void DMessage::SendMetadata(const char *url, int64_t *duration)
{
  DBusMessage *reply = dbus_message_new_method_return(m);
  if(reply)
  {
    //Create iterator: Array of dict entries, composed of string (key)) and variant (value)
    DBusMessageIter array_cont, dict_cont, dict_entry_cont, var;
    dbus_message_iter_init_append(reply, &array_cont);
    dbus_message_iter_open_container(&array_cont, DBUS_TYPE_ARRAY, "{sv}", &dict_cont);

      //First dict entry: URI
      const char *key1 = "xesam:url";
      dbus_message_iter_open_container(&dict_cont, DBUS_TYPE_DICT_ENTRY, nullptr, &dict_entry_cont);
        dbus_message_iter_append_basic(&dict_entry_cont, DBUS_TYPE_STRING, &key1);
        dbus_message_iter_open_container(&dict_entry_cont, DBUS_TYPE_VARIANT, DBUS_TYPE_STRING_AS_STRING, &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &url);
        dbus_message_iter_close_container(&dict_entry_cont, &var);
      dbus_message_iter_close_container(&dict_cont, &dict_entry_cont);

      //Second dict entry: duration in us
      const char *key2 = "mpris:length";
      dbus_message_iter_open_container(&dict_cont, DBUS_TYPE_DICT_ENTRY, nullptr, &dict_entry_cont);
        dbus_message_iter_append_basic(&dict_entry_cont, DBUS_TYPE_STRING, &key2);
        dbus_message_iter_open_container(&dict_entry_cont, DBUS_TYPE_VARIANT, DBUS_TYPE_INT64_AS_STRING, &var);
        dbus_message_iter_append_basic(&var, DBUS_TYPE_INT64, duration);
        dbus_message_iter_close_container(&dict_entry_cont, &var);
      dbus_message_iter_close_container(&dict_cont, &dict_entry_cont);
    dbus_message_iter_close_container(&array_cont, &dict_cont);

    //Send message
    needs_response = false;
    dbus_connection_send(bus, reply, nullptr);
    dbus_message_unref(reply);
  }
}
