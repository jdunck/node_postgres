#include <libpq-fe.h>
#include "type-oids.h"
#include <node/node.h>
#include <node/events.h>
#include <assert.h>

using namespace v8;
using namespace node;

#define READY_STATE_SYMBOL String::NewSymbol("readyState")

class Connection : public EventEmitter {
 public:
  static void
  Initialize (v8::Handle<v8::Object> target)
  {
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(New);

    t->Inherit(EventEmitter::constructor_template);
    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(t, "connect", Connect);
    NODE_SET_PROTOTYPE_METHOD(t, "close", Close);
    NODE_SET_PROTOTYPE_METHOD(t, "reset", Reset);
    NODE_SET_PROTOTYPE_METHOD(t, "dispatchQuery", DispatchQuery);

    t->PrototypeTemplate()->SetAccessor(READY_STATE_SYMBOL, ReadyStateGetter);

    target->Set(String::NewSymbol("Connection"), t->GetFunction());
  }

  bool Connect (const char* conninfo)
  {
    if (connection_) return false;
    
    /* TODO Ensure that hostaddr is always specified in conninfo to avoid
     * name lookup. (Unless we're connecting to localhost.)
     */
    connection_ = PQconnectStart(conninfo);
    if (!connection_) return false;

    if (PQsetnonblocking(connection_, 1) == -1) {
      PQfinish(connection_);
      connection_ = NULL;
      return false;
    }

    ConnStatusType status = PQstatus(connection_);

    if (CONNECTION_BAD == status) {
      PQfinish(connection_);
      connection_ = NULL;
      return false;
    }

    connecting_ = true;

    int fd = PQsocket(connection_);

    ev_io_set(&read_watcher_, fd, EV_READ);
    ev_io_set(&write_watcher_, fd, EV_WRITE);

    /* If you have yet to call PQconnectPoll, i.e., just after the call to
     * PQconnectStart, behave as if it last returned PGRES_POLLING_WRITING.
     */
    ev_io_start(EV_DEFAULT_ &write_watcher_);
    
    Attach();

    return true;
  }

  bool Reset ( )
  {
    /* To initiate a connection reset, call PQresetStart. If it returns 0,
     * the reset has failed. If it returns 1, poll the reset using
     * PQresetPoll in exactly the same way as you would create the
     * connection using PQconnectPoll */
    int r = PQresetStart(connection_);
    if (r == 0) return false;

    if (PQsetnonblocking(connection_, 1) == -1) {
      PQfinish(connection_);
      connection_ = NULL;
      return false;
    }

    resetting_ = true;
    ev_io_set(&read_watcher_, PQsocket(connection_), EV_READ);
    ev_io_set(&write_watcher_, PQsocket(connection_), EV_WRITE);
    ev_io_start(EV_DEFAULT_ &write_watcher_);
    return true;
  }

  bool Query (const char *command) 
  {
    int r = PQsendQuery(connection_, command);
    if (r == 0) {
      return false;
    }
    if (PQflush(connection_) == 1) ev_io_start(&write_watcher_);
    return true;
  }

  void Close (Local<Value> exception = Local<Value>())
  {
    HandleScope scope;
    ev_io_stop(EV_DEFAULT_ &write_watcher_);
    ev_io_stop(EV_DEFAULT_ &read_watcher_);
    PQfinish(connection_);
    connection_ = NULL;
    if (exception.IsEmpty()) {
      Emit("close", 0, NULL);
    } else {
      Emit("close", 1, &exception);
    }
    Detach();
  }

  char * ErrorMessage ( )
  {
    return PQerrorMessage(connection_);
  }

 protected:

  static Handle<Value>
  New (const Arguments& args)
  {
    HandleScope scope;

    Connection *connection = new Connection();
    connection->Wrap(args.This());

    return args.This();
  }

  static Handle<Value>
  Connect (const Arguments& args)
  {
    Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

    HandleScope scope;

    if (args.Length() == 0 || !args[0]->IsString()) {
      return ThrowException(String::New("Must give conninfo string as argument"));
    }

    String::Utf8Value conninfo(args[0]->ToString());

    bool r = connection->Connect(*conninfo);

    if (!r) {
      return ThrowException(Exception::Error(
            String::New(connection->ErrorMessage())));
    }

    return Undefined();
  }

  static Handle<Value>
  Close (const Arguments& args)
  {
    Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());
    HandleScope scope;
    connection->Close();
    return Undefined();
  }

  static Handle<Value>
  Reset (const Arguments& args)
  {
    Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

    HandleScope scope;

    bool r = connection->Reset();

    if (!r) {
      return ThrowException(Exception::Error(
            String::New(connection->ErrorMessage())));
    }

    return Undefined();
  }

  static Handle<Value>
  DispatchQuery (const Arguments& args)
  {
    Connection *connection = ObjectWrap::Unwrap<Connection>(args.This());

    HandleScope scope;

    if (args.Length() == 0 || !args[0]->IsString()) {
      return ThrowException(Exception::TypeError(
            String::New("First argument must be a string")));
    }

    String::Utf8Value query(args[0]->ToString());

    bool r = connection->Query(*query);

    if (!r) {
      return ThrowException(Exception::Error(
            String::New(connection->ErrorMessage())));
    }

    return Undefined();
  }

  static Handle<Value>
  ReadyStateGetter (Local<String> property, const AccessorInfo& info)
  {
    Connection *connection = ObjectWrap::Unwrap<Connection>(info.This());
    assert(connection);

    assert(property == READY_STATE_SYMBOL);

    HandleScope scope;

    const char *s;

    switch (PQstatus(connection->connection_)) {
      case CONNECTION_STARTED: 
        s = "started";
        break;
      case CONNECTION_MADE: 
        s = "made";
        break;
      case CONNECTION_AWAITING_RESPONSE:
        s = "awaitingResponse";
        break;
      case CONNECTION_OK:
        s = "OK";
        break;
      case CONNECTION_AUTH_OK:
        s = "authOK";
        break;
      case CONNECTION_SSL_STARTUP:
        s = "sslStartup";
        break;
      case CONNECTION_SETENV:
        s = "setEnv";
        break;
      case CONNECTION_BAD:
        s = "bad";
        break;
      case CONNECTION_NEEDED:
        s = "needed";
        break;
    }

    return scope.Close(String::NewSymbol(s));
  }

  Connection () : EventEmitter () 
  {
    connection_ = NULL;

    connecting_ = resetting_ = false;

    ev_init(&read_watcher_, io_event);
    read_watcher_.data = this;

    ev_init(&write_watcher_, io_event);
    write_watcher_.data = this;
  }

  ~Connection ()
  {
    assert(connection_ == NULL);
  }

 private:

  void MakeConnection ()
  {
    PostgresPollingStatusType status;
    if (connecting_) {
     status = PQconnectPoll(connection_);
    } else {
     assert(resetting_);
     status = PQresetPoll(connection_);
    }

    if (status == PGRES_POLLING_READING) {
      ev_io_stop(EV_DEFAULT_ &write_watcher_);
      ev_io_start(EV_DEFAULT_ &read_watcher_);
      return;

    } else if (status == PGRES_POLLING_WRITING) {
      ev_io_stop(EV_DEFAULT_ &read_watcher_);
      ev_io_start(EV_DEFAULT_ &write_watcher_);
      return;
    }

    if (status == PGRES_POLLING_OK) {
      Emit("connect", 0, NULL);
      connecting_ = resetting_ = false;
      ev_io_start(EV_DEFAULT_ &read_watcher_);
      return;
    }
    
    CloseConnectionWithError();
  }

  Local<Value> BuildCell (PGresult *result, int row, int col)
  {
    HandleScope scope;

    if (PQgetisnull(result, row, col)) return scope.Close(Null());

    char *string = PQgetvalue(result, row, col);
    int32_t n = 0, i = 0;

    Oid t = PQftype(result, col);

    Handle<Value> cell;

    switch (t) {
      case BOOLOID:
        cell = *string == 't' ? True() : False();
        break;

      case INT8OID:
      case INT4OID:
      case INT2OID:
        for (i = string[0] == '-' ? 1 : 0; string[i]; i++) {
          n *= 10;
          n += string[i] - '0';
        }
        if (string[0] == '-') n = -n;
        cell = Integer::New(n);
        break;

      default:
#ifndef NDEBUG
        printf("Unhandled OID: %d\n", t);
#endif
        cell = String::New(string);
    }
    return scope.Close(cell); 
  }

  Local<Value> BuildTuples (PGresult *result)
  {
    HandleScope scope;

    int nrows = PQntuples(result);
    int ncols = PQnfields(result);
    int row_index, col_index;

    Local<Array> tuples = Array::New(nrows);

    for (row_index = 0; row_index < nrows; row_index++) {
      Local<Array> row = Array::New(ncols);
      tuples->Set(Integer::New(row_index), row);

      for (col_index = 0; col_index < ncols; col_index++) {
        Local<Value> cell = BuildCell(result, row_index, col_index); 
        row->Set(Integer::New(col_index), cell);
      }
    }

    return scope.Close(tuples);
  }

  void CloseConnectionWithError (const char *message_s = NULL)
  {
    HandleScope scope;

    if (!message_s) message_s = PQerrorMessage(connection_);
    Local<String> message = String::New(message_s);
    Local<Value> exception = Exception::Error(message);

    Close(exception);
  }

  static inline
  Local<Value> BuildResultException (PGresult *result)
  {
    HandleScope scope;

    char *primary_s = PQresultErrorField(result, PG_DIAG_MESSAGE_PRIMARY);
    assert(primary_s);
    Local<String> primary = String::New(primary_s);

    Local<Value> error_v = Exception::Error(primary);
    assert(error_v->IsObject());
    Local<Object> error = Local<Object>::Cast(error_v);

    char *full_s = PQresultErrorMessage(result);
    if (full_s) {
      error->Set(String::NewSymbol("full"), String::New(full_s));
    }

    char *detail_s = PQresultErrorField(result, PG_DIAG_MESSAGE_DETAIL);
    if (detail_s) {
      error->Set(String::NewSymbol("detail"), String::New(detail_s));
    }

    char *severity_s = PQresultErrorField(result, PG_DIAG_SEVERITY);
    if (severity_s) {
      error->Set(String::NewSymbol("severity"), String::New(severity_s));
    }

    /* TODO PG_DIAG_SQLSTATE
     *      PG_DIAG_MESSAGE_HINT
     *      PG_DIAG_STATEMENT_POSITION
     *      PG_DIAG_INTERNAL_POSITION
     *      PG_DIAG_INTERNAL_QUERY
     *      PG_DIAG_CONTEXT
     *      PG_DIAG_SOURCE_FILE
     *      PG_DIAG_SOURCE_LINE
     *      PG_DIAG_SOURCE_FUNCTION
     */

    return scope.Close(error);
  }

  void EmitResult (PGresult *result)
  {
    Local<Value> tuples;
    Local<Value> exception;

    switch (PQresultStatus(result)) {
      case PGRES_EMPTY_QUERY:
      case PGRES_COMMAND_OK:
        Emit("result", 0, NULL);
        break;

      case PGRES_TUPLES_OK:
        tuples = BuildTuples(result);
        Emit("result", 1, &tuples);
        break;

      case PGRES_COPY_OUT:
      case PGRES_COPY_IN:
        assert(0 && "Not yet implemented.");
        exception = Exception::Error(String::New("Not yet implemented"));
        Emit("result", 1, &exception);
        break;

      case PGRES_BAD_RESPONSE:
      case PGRES_NONFATAL_ERROR:
      case PGRES_FATAL_ERROR:
        exception = BuildResultException(result);
        Emit("result", 1, &exception);
        break;
    }
  }

  void Event (int revents)
  {
    if (revents & EV_ERROR) {
      CloseConnectionWithError("connection closed");
      return;
    }

    assert(PQisnonblocking(connection_));

    if (connecting_ || resetting_) {
      MakeConnection();
      return;
    }

    if (revents & EV_READ) {
      if (PQconsumeInput(connection_) == 0) {
        CloseConnectionWithError();
        return;
      }

      if (!PQisBusy(connection_)) {
        PGresult *result;
        while ((result = PQgetResult(connection_))) {
          EmitResult(result);
          PQclear(result);
        }
        Emit("ready", 0, NULL);
      }      
    }

    if (revents & EV_WRITE) {
      if (PQflush(connection_) == 0) ev_io_stop(&write_watcher_);
    }
  }

  static void
  io_event (EV_P_ ev_io *w, int revents)
  {
    Connection *connection = static_cast<Connection*>(w->data);
    connection->Event(revents);
  }

  ev_io read_watcher_;
  ev_io write_watcher_;
  PGconn *connection_;
  bool connecting_;
  bool resetting_;
};

extern "C" void
init (Handle<Object> target) 
{
  HandleScope scope;
  Connection::Initialize(target);
}
