/* TODO: Format with proper coding style */
#include <errno.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>

#define _REALLY_INCLUDE_SYS__SYSTEM_PROPERTIES_H_

#include <api/system_properties.h>
#include <unistd.h>
#include <async_safe/log.h>

#include "private/bionic_defs.h"
#include "platform/bionic/macros.h"

#ifndef PROP_MSG_SETPROP
  #define PROP_MSG_SETPROP 1u
#endif
#ifndef PROP_MSG_SETPROP2
  #define PROP_MSG_SETPROP2 2u
#endif
#ifndef PROP_SUCCESS
  #define PROP_SUCCESS 0
#endif

static const char property_service_socket[] = "/dev/socket/" PROP_SERVICE_NAME;
static const char *kServiceVersionPropertyName = "ro.property_service.version";

typedef struct PropertyServiceConnection {
  int socket;
  int last_error;
} PropertyServiceConnection;

static void property_service_connection_init(PropertyServiceConnection *connection) {
  connection->last_error = 0;
  connection->socket = socket(AF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (connection->socket == -1) {
    connection->last_error = errno;

    return;
  }

  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  strlcpy(addr.sun_path, property_service_socket, sizeof(addr.sun_path));
  addr.sun_family = AF_LOCAL;

  const size_t namelen = strlen(property_service_socket);
  socklen_t alen = namelen + offsetof(struct sockaddr_un, sun_path) + 1;

  if (TEMP_FAILURE_RETRY(connect(connection->socket, (struct sockaddr *)&addr, alen)) == -1) {
    connection->last_error = errno;

    close(connection->socket);
    connection->socket = -1;
  }
}

static void property_service_connection_close(PropertyServiceConnection *connection) {
  if (connection->socket != -1) {
    close(connection->socket);
    connection->socket = -1;
  }
}

static bool property_service_connection_is_valid(PropertyServiceConnection *connection) {
  return connection->socket != -1;
}

static bool property_service_connection_recv_int32(PropertyServiceConnection *connection, int32_t *value) {
  int result = TEMP_FAILURE_RETRY(recv(connection->socket, value, sizeof(*value), MSG_WAITALL));
  if (result == -1) connection->last_error = errno;
  else if (result != (int)sizeof(*value)) connection->last_error = -1;
  else connection->last_error = 0;

  return connection->last_error == 0;
}

typedef struct SocketWriter {
  PropertyServiceConnection *connection;
  struct iovec iov[8];
  size_t iov_index;
  uint32_t uint_buf[8];
  size_t uint_buf_index;
} SocketWriter;

static void socket_writer_init(SocketWriter *writer, PropertyServiceConnection *connection) {
  writer->connection = connection;
  writer->iov_index = 0;
  writer->uint_buf_index = 0;
}

static bool socket_writer_write_uint32(SocketWriter *writer, uint32_t value) {
  if (writer->uint_buf_index >= 8 || writer->iov_index >= 8) {
    writer->connection->last_error = EMSGSIZE;

    return false;
  }

  uint32_t *ptr = writer->uint_buf + writer->uint_buf_index;
  writer->uint_buf[writer->uint_buf_index++] = value;
  writer->iov[writer->iov_index].iov_base = ptr;
  writer->iov[writer->iov_index].iov_len = sizeof(*ptr);
  ++writer->iov_index;

  return true;
}

static bool socket_writer_write_string(SocketWriter *writer, const char *value) {
  uint32_t valuelen = strlen(value);
  if (!socket_writer_write_uint32(writer, valuelen)) return false;

  if (valuelen == 0) return true;

  if (writer->iov_index >= 8) {
    writer->connection->last_error = EMSGSIZE;

    return false;
  }

  writer->iov[writer->iov_index].iov_base = (void *)value;
  writer->iov[writer->iov_index].iov_len = valuelen;
  ++writer->iov_index;

  return true;
}

static bool socket_writer_send(SocketWriter *writer) {
  if (!property_service_connection_is_valid(writer->connection)) {
    return false;
  }

  if (writev(writer->connection->socket, writer->iov, writer->iov_index) == -1) {
    writer->connection->last_error = errno;

    return false;
  }

  writer->iov_index = writer->uint_buf_index = 0;
  return true;
}

typedef struct prop_msg {
  unsigned cmd;
  char name[PROP_NAME_MAX];
  char value[PROP_VALUE_MAX];
} prop_msg;

static int send_prop_msg(const prop_msg *msg) {
  PropertyServiceConnection connection;
  property_service_connection_init(&connection);
  if (!property_service_connection_is_valid(&connection)) {
    int err = connection.last_error;
    property_service_connection_close(&connection);

    return err;
  }

  int result = -1;
  int s = connection.socket;
  const int num_bytes = TEMP_FAILURE_RETRY(send(s, msg, sizeof(prop_msg), 0));
  if (num_bytes == (int)sizeof(prop_msg)) {
    // We successfully wrote to the property server but now we
    // wait for the property server to finish its work.  It
    // acknowledges its completion by closing the socket so we
    // poll here (on nothing), waiting for the socket to close.
    // If you 'adb shell setprop foo bar' you'll see the POLLHUP
    // once the socket closes.  Out of paranoia we cap our poll
    // at 250 ms.
    struct pollfd pollfds[1] = { 
      {
        .fd = -1,
        .events = 0,
        .revents = 0,
      }
    };
    const int poll_result = TEMP_FAILURE_RETRY(poll(pollfds, 1, 250 /* ms */));
    if (poll_result == 1 && (pollfds[0].revents & POLLHUP) != 0) {
      result = 0;
    } else {
      // Ignore the timeout and treat it like a success anyway.
      // The init process is single-threaded and its property
      // service is sometimes slow to respond (perhaps it's off
      // starting a child process or something) and thus this
      // times out and the caller thinks it failed, even though
      // it's still getting around to it.  So we fake it here,
      // mostly for ctl.* properties, but we do try and wait 250
      // ms so callers who do read-after-write can reliably see
      // what they've written.  Most of the time.
      async_safe_format_log(ANDROID_LOG_WARN, "libc", "Property service has timed out while trying to set \"%s\" to \"%s\"", msg->name, msg->value);

      result = 0;
    }
  }

  property_service_connection_close(&connection);
  return result;
}

static const uint32_t kProtocolVersion1 = 1;
static const uint32_t kProtocolVersion2 = 2;  // current
static atomic_uint_least32_t g_propservice_protocol_version = 0;

static void detect_protocol_version(void) {
  char value[PROP_VALUE_MAX];
  if (__system_property_get(kServiceVersionPropertyName, value) == 0) {
    async_safe_format_log(ANDROID_LOG_WARN, "libc", "Using old property service protocol (\"%s\" is not set)", kServiceVersionPropertyName);

    g_propservice_protocol_version = kProtocolVersion1;
  } else {
    uint32_t version = (uint32_t)atoll(value);
    if (version >= kProtocolVersion2) {
      g_propservice_protocol_version = kProtocolVersion2;
    } else {
      async_safe_format_log(ANDROID_LOG_WARN, "libc", "Using old property service protocol (\"%s\"=\"%s\")", kServiceVersionPropertyName, value);

      g_propservice_protocol_version = kProtocolVersion1;
    }
  }
}

__BIONIC_WEAK_FOR_NATIVE_BRIDGE int __system_property_set(const char *key, const char *value) {
  if (key == NULL) return -1;
  if (value == NULL) value = "";

  if (g_propservice_protocol_version == 0) detect_protocol_version();

  if (g_propservice_protocol_version == kProtocolVersion1) {
    // Old protocol does not support long names or values
    if (strlen(key) >= PROP_NAME_MAX) return -1;
    if (strlen(value) >= PROP_VALUE_MAX) return -1;

    prop_msg msg = {
      .cmd = PROP_MSG_SETPROP,
      .name = "",
      .value = "",
    };

    strlcpy(msg.name, key, sizeof msg.name);
    strlcpy(msg.value, value, sizeof msg.value);

    return send_prop_msg(&msg);
  }

  // New protocol only allows long values for ro. properties only.
  if (strlen(value) >= PROP_VALUE_MAX && strncmp(key, "ro.", 3) != 0) return -1;

  PropertyServiceConnection connection;
  property_service_connection_init(&connection);
  if (!property_service_connection_is_valid(&connection)) {
    errno = connection.last_error;
    async_safe_format_log(ANDROID_LOG_WARN, "libc", "Unable to set property \"%s\" to \"%s\": connection failed; errno=%d (%s)", key, value, errno, strerror(errno));

    property_service_connection_close(&connection);

    return -1;
  }

  SocketWriter writer;
  socket_writer_init(&writer, &connection);
  if (!socket_writer_write_uint32(&writer, PROP_MSG_SETPROP2) ||
      !socket_writer_write_string(&writer, key) ||
      !socket_writer_write_string(&writer, value) ||
      !socket_writer_send(&writer)) {
    errno = connection.last_error;
    async_safe_format_log(ANDROID_LOG_WARN, "libc", "Unable to set property \"%s\" to \"%s\": write failed; errno=%d (%s)", key, value, errno, strerror(errno));

    property_service_connection_close(&connection);

    return -1;
  }

  int result = -1;
  if (!property_service_connection_recv_int32(&connection, &result)) {
    errno = connection.last_error;
    async_safe_format_log(ANDROID_LOG_WARN, "libc", "Unable to set property \"%s\" to \"%s\": recv failed; errno=%d (%s)", key, value, errno, strerror(errno));

    property_service_connection_close(&connection);

    return -1;
  }

  property_service_connection_close(&connection);
  if (result != PROP_SUCCESS) {
    async_safe_format_log(ANDROID_LOG_WARN, "libc", "Unable to set property \"%s\" to \"%s\": error code: 0x%x", key, value, result);

    return -1;
  }

  return 0;
}
