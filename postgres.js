var binding = require("binding.node");

var Connection = binding.Connection;

// postgres cannot handle multiple queries at the same time.
// thus we must queue them internally and dispatch them as
// others come in. 
Connection.prototype.maybeDispatchQuery = function () {
  if (!this._queries) return;
  // If not connected, do not dispatch. 
  if (this.readyState != "OK") return;
  if (!this.currentQuery && this._queries.length > 0) {
    this.currentQuery = this._queries.shift();
    this.dispatchQuery(this.currentQuery.sql);
  }
};

Connection.prototype.query = function (sql) {
  if (!this._queries) this._queries = [];
  var promise = new node.Promise;
  promise.sql = sql;
  this._queries.push(promise);
  this.maybeDispatchQuery();
  return promise;
};

exports.createConnection = function (conninfo) {
  var c = new Connection;

  c.addListener("connect", function () {
    c.maybeDispatchQuery();
  });

  c.addListener("result", function (arg) {
    node.assert(c.currentQuery);
    var promise = c.currentQuery;
    c.currentQuery = null;
    if (arg instanceof Error)  {
      promise.emitError([arg]);
    } else {
      promise.emitSuccess([arg]);
    }
  });

  c.addListener("ready", function () {
    c.maybeDispatchQuery();
  });

  c.connect(conninfo);

  return c;
};
