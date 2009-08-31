#include <libpq-fe.h>
#include <node/node.h>
#include <node/events.h>

using namespace v8;
using namespace node;

#define READY_STATE_SYMBOL String::NewSymbol("readyState")

class PostgresConnection : public EventEmitter {
 public:
  static void
  Initialize (v8::Handle<v8::Object> target)
  {
    HandleScope scope;

    Local<FunctionTemplate> t = FunctionTemplate::New(New);

    //constructor_template = Persistent<FunctionTemplate>::New(t);
    t->Inherit(EventEmitter::constructor_template);
    t->InstanceTemplate()->SetInternalFieldCount(1);

    NODE_SET_PROTOTYPE_METHOD(t, "connect", Connect);

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

    if (PQsetnonblocking(connection_, 1) == -1) goto error;

    if (CONNECTION_BAD == PQstatus(connection_)) goto error; 

    connecting_ = true;

    ev_io_set(&read_watcher_, PQsocket(connection_), EV_READ);
    ev_io_set(&write_watcher_, PQsocket(connection_), EV_WRITE);

    /* If you have yet to call PQconnectPoll, i.e., just after the call to
     * PQconnectStart, behave as if it last returned PGRES_POLLING_WRITING.
     */
    ev_io_start(EV_DEFAULT_ &write_watcher_);
    Attach();
    return true;

  error:
    PQfinish(connection_);
    connection_ = NULL;
    return false;
  }

  bool Reset ( )
  {
    /* To initiate a connection reset, call PQresetStart. If it returns 0,
     * the reset has failed. If it returns 1, poll the reset using
     * PQresetPoll in exactly the same way as you would create the
     * connection using PQconnectPoll */
    int r = PQresetStart(connection_);
    if (r == 0) return false;
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

  void Finish ( )
  {
    PQfinish(connection_);
    connection_ = NULL;
    Detach();
  }

 protected:

  static Handle<Value>
  New (const Arguments& args)
  {
    HandleScope scope;

    PostgresConnection *connection = new PostgresConnection();
    connection->Wrap(args.This());

    return args.This();
  }

  static Handle<Value>
  Connect (const Arguments& args)
  {
    PostgresConnection *connection = ObjectWrap::Unwrap<PostgresConnection>(args.This());

    HandleScope scope;

    if (args.Length() == 0 || !args[0]->IsString()) {
      return ThrowException(String::New("Must give conninfo string as argument"));
    }

    String::Utf8Value conninfo(args[0]->ToString());

    bool r = connection->Connect(*conninfo);

    if (!r) {
      return ThrowException(String::New("Error opening connection."));
    }

    return Undefined();
  }

  static Handle<Value>
  ReadyStateGetter (Local<String> property, const AccessorInfo& info)
  {
    PostgresConnection *connection = ObjectWrap::Unwrap<PostgresConnection>(info.This());
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

  PostgresConnection ( ) : EventEmitter () 
  {
    connection_ = NULL;

    connecting_ = resetting_ = false;

    ev_init(&read_watcher_, io_event);
    read_watcher_.data = this;

    ev_init(&write_watcher_, io_event);
    write_watcher_.data = this;
  }

  void ConnectEvent ()
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
    } else if (status == PGRES_POLLING_WRITING) {
      ev_io_stop(EV_DEFAULT_ &write_watcher_);
      ev_io_start(EV_DEFAULT_ &read_watcher_);
    }

    connecting_ = resetting_ = false;

    if (status == PGRES_POLLING_OK) {
      if (connecting_) {
        Emit("connect", 0, NULL);
      } else {
        Emit("reset", 0, NULL);
      }
      ev_io_start(EV_DEFAULT_ &read_watcher_);
      return;
    }
    
    Emit("error", 0, NULL);
    Finish();
  }

 private:
  void EmitResult (PGresult *result)
  {
    fprintf(stderr, "EmitResult");
    switch (PQresultStatus(result)) {
      case PGRES_EMPTY_QUERY:
      case PGRES_COMMAND_OK:
        Emit("result", 0, NULL);
        break;

      case PGRES_TUPLES_OK:
      case PGRES_COPY_OUT:
      case PGRES_COPY_IN:
        assert(0 && "Not yet implemented.");
        break;

      case PGRES_BAD_RESPONSE:
      case PGRES_NONFATAL_ERROR:
      case PGRES_FATAL_ERROR:
        break;
    }
  }

  void Event (int revents)
  {
    assert(PQisnonblocking(connection_));

    if (!connecting_) {
      ConnectEvent();
      return;
    }

    if (revents & EV_READ) {
      if (PQconsumeInput(connection_) == 0) {
        Emit("error", 0, NULL);
        Finish();
        return;
      }
      
      PGresult *result;
      while ((result = PQgetResult(connection_))) {
        EmitResult(result);
        PQclear(result);
      }
    }

    if (revents & EV_WRITE) {
      if (PQflush(connection_) == 0) ev_io_stop(&write_watcher_);
    }
  }

  static void
  io_event (EV_P_ ev_io *w, int revents)
  {
    PostgresConnection *connection = static_cast<PostgresConnection*>(w->data);
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
  target->Set(String::New("hello"), String::New("world"));
  PostgresConnection::Initialize(target);
}
