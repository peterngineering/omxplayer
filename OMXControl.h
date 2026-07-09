#pragma once

#include <stdint.h>
#include <vector>
#include <string>

#include "utils/NoMoveCopy.h"
#include "omxplayer.h"

struct DBusMessage;
struct DBusMessageIter;

class DMessage : NoMoveCopy
{
private:
  DBusMessage *m;
  DBusMessageIter *m_args = nullptr;

public:
  DMessage(DBusMessage *message, bool needs_response);
  ~DMessage();

  operator DBusMessage*() const { return m; }

  bool GetArgInt(int *value);
  bool GetArgInt64(int64_t *value);
  bool GetArgDouble(double *value);
  bool GetArgString(std::string &value);

  bool IgnoreArg();

  void RespondUnknownProperty();
  void RespondUnknownMethod();
  void RespondInvalidArgs();

  void RespondInt64(int64_t value);
  void RespondDouble(double value);
  void RespondBool(bool value);
  void RespondString(const std::string &value);

  void RespondArray(const std::vector<std::string> &list);
  void RespondArray(const char *array[], int size);

  void SendMetadata(const char *url, int64_t *duration);

private:
  void Respond(int type, void *value);
  bool GetArg(int type, void *value);
  void RespondError(const char *name, const char *msg);

  bool needs_response;
};


class OMXControl : NoMoveCopy
{
public:
  ~OMXControl();
  bool Connect(const char *dbus_name);
  enum ControlFlow GetEvent();
  operator bool() const;
private:
  void Dispatch();
  bool DbusConnect(const char *dbus_name);
  void DbusDisconnect();
};
