// volt2 page client. The same file serves both builds:
//   development: volt2.startDev() talks to your native program over synchronous HTTP;
//   WebAssembly: the module calls volt2.attachWasm() and talks to it directly.
//
// C++ asks for operations (get, set, call, ...) and this file runs them; JavaScript
// calls C++ functions through callNative. Values cross as JSON: booleans, finite
// numbers, strings and null as themselves; undefined as {u:1}; NaN and the
// infinities as {f:"NaN"}; anything else as {h:id}, a handle that C++ releases
// when it no longer needs it.
//
// volt2.hpp embeds a copy of this file. After editing it, run tools/embed_js.py.
(function () {
  'use strict';

  const handles = new Map();
  let nextHandle = 1;
  let transport = null; // (message) => reply; set by startDev or attachWasm

  function toWire(value) {
    switch (typeof value) {
      case 'undefined':
        return { u: 1 };
      case 'boolean':
      case 'string':
        return value;
      case 'number':
        return Number.isFinite(value) ? value : { f: String(value) };
      default: {
        if (value === null) return null;
        const id = nextHandle++;
        handles.set(id, value);
        return { h: id };
      }
    }
  }

  function fromWire(wire) {
    if (wire === null || typeof wire !== 'object') return wire;
    if ('h' in wire) {
      if (!handles.has(wire.h)) throw new Error('volt2: JavaScript value #' + wire.h + ' was already released');
      return handles.get(wire.h);
    }
    if ('u' in wire) return undefined;
    if ('f' in wire) return Number(wire.f);
    throw new Error('volt2: malformed value ' + JSON.stringify(wire));
  }

  function release(ids) {
    if (ids) for (const id of ids) handles.delete(id);
  }

  function kind(value) {
    return value === null ? 'null' : typeof value;
  }

  function perform(op) {
    const args = op.a ? op.a.map(fromWire) : [];
    switch (op.op) {
      case 'global':
        return op.k === undefined ? globalThis : globalThis[op.k];
      case 'get':
        return fromWire(op.o)[fromWire(op.k)];
      case 'set':
        fromWire(op.o)[fromWire(op.k)] = fromWire(op.v);
        return undefined;
      case 'call': {
        const target = fromWire(op.o);
        const method = target[op.k];
        if (typeof method !== 'function') {
          throw new TypeError("'" + op.k + "' is not a function (it is " + kind(method) + ')');
        }
        return Reflect.apply(method, target, args);
      }
      case 'invoke': {
        const fn = fromWire(op.o);
        if (typeof fn !== 'function') throw new TypeError(kind(fn) + ' is not a function');
        return Reflect.apply(fn, undefined, args);
      }
      case 'new':
        return Reflect.construct(fromWire(op.o), args);
      case 'typeof':
        return typeof fromWire(op.o);
      case 'eq':
        return fromWire(op.o) === fromWire(op.v);
      case 'fn': {
        const id = op.k;
        return function () {
          return callNative(id, Array.prototype.slice.call(arguments));
        };
      }
      default:
        throw new Error('volt2: unknown operation ' + op.op);
    }
  }

  // Runs one operation for C++. Errors are returned, never thrown across the bridge.
  // Released handles are dropped afterwards, so a reply may still refer to them.
  function exec(op) {
    try {
      return { v: toWire(perform(op)) };
    } catch (e) {
      return { error: e instanceof Error ? e.name + ': ' + e.message : String(e) };
    } finally {
      release(op.rel);
    }
  }

  // Sends a message to C++ and waits for its return value. In development the
  // native side may first answer with operations; we run them and keep waiting,
  // which is what lets calls nest in both directions.
  function converse(message) {
    for (;;) {
      const reply = transport(message);
      if (reply.t === 'op') {
        message = Object.assign({ t: 'result' }, exec(reply));
        continue;
      }
      const failed = 'error' in reply;
      const value = failed ? undefined : fromWire(reply.v);
      release(reply.rel);
      if (failed) throw new Error(reply.error);
      return value;
    }
  }

  function callNative(id, args) {
    if (!transport) throw new Error('volt2: not connected to C++');
    return converse({ t: 'call', id: id, a: args.map(toWire) });
  }

  function showProblem(error) {
    const box = document.createElement('pre');
    box.style.cssText = 'color:#b00020;background:#fff0f0;border:1px solid #b00020;padding:12px;white-space:pre-wrap';
    box.textContent = String((error && error.message) || error);
    document.body.appendChild(box);
  }

  function startDev() {
    const page = Date.now().toString(36) + Math.random().toString(36).slice(2);
    transport = function (message) {
      message.s = page;
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/volt2/bridge', false); // synchronous on purpose, see the README
      xhr.setRequestHeader('Content-Type', 'application/json');
      try {
        xhr.send(JSON.stringify(message));
      } catch (e) {
        throw new Error('volt2: cannot reach your program. Is it still running?');
      }
      if (xhr.status !== 200) throw new Error('volt2: your program answered HTTP ' + xhr.status);
      return JSON.parse(xhr.responseText);
    };
    try {
      converse({ t: 'start' });
    } catch (e) {
      showProblem(e);
      throw e;
    }
  }

  function attachWasm(dispatch) {
    transport = function (message) {
      return JSON.parse(dispatch(JSON.stringify(message)));
    };
  }

  function execJson(text) {
    return JSON.stringify(exec(JSON.parse(text)));
  }

  globalThis.volt2 = {
    startDev: startDev,
    attachWasm: attachWasm,
    execJson: execJson,
    handleCount: function () {
      return handles.size;
    },
  };
})();
