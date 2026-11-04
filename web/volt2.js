// volt2 page client. The same file serves both builds:
//   development: volt2.startDev() talks to your native program over synchronous HTTP;
//   WebAssembly: the module calls volt2.attachWasm() and talks to it directly.
//
// C++ asks for operations (get, set, call, ...) and this file runs them; JavaScript
// calls C++ functions through callNative. Values cross as JSON: booleans, finite
// numbers, strings and null as themselves; undefined as {u:1}; NaN and the
// infinities as {f:"NaN"}; anything else as {h:id}, a handle that C++ releases
// when it no longer needs it. An object keeps the same id for as long as C++ holds
// it, so C++ can compare objects without asking the page.
//
// volt2.hpp embeds a copy of this file. After editing it, run tools/embed_js.py.
(function () {
  'use strict';

  const handles = new Map(); // id -> value
  const idOf = new Map(); // value -> id, for values C++ currently holds
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
        let id = idOf.get(value);
        if (id === undefined) {
          id = nextHandle++; // never reused, so a released id cannot mean another object
          handles.set(id, value);
          idOf.set(value, id);
        }
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
    if (!ids) return;
    for (const id of ids) {
      if (handles.has(id)) idOf.delete(handles.get(id));
      handles.delete(id);
    }
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
      case 'patch':
        return patch(op);
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

  // Rendering. C++ keeps the tree it rendered last and sends one patch per render with
  // what changed (see Renderer::renderNow in volt2.hpp for its format). Every DOM node
  // volt2 made has a number; 0 is the mount, #volt2, created at the end of <body> if the
  // page has none. One capturing listener per event type on the mount sends C++ the
  // numbers of the elements with handlers for that event, from the target upward (only
  // the target when the event does not bubble).
  const nodes = new Map(); // number -> DOM node
  const numberOf = new WeakMap(); // DOM node -> number
  const handled = new WeakMap(); // element -> the event types it has C++ handlers for
  let listening = new Set(); // event types the mount listens to
  let mount = null;
  let dispatcher = null; // C++: runs the handlers for an event
  let patching = false; // events the patch itself causes, such as focus moving, are not sent

  function mountElement() {
    let element = document.getElementById('volt2');
    if (!element) {
      element = document.createElement('div');
      element.id = 'volt2';
      document.body.appendChild(element);
    }
    return element;
  }

  function nodeNumbered(number) {
    const node = nodes.get(number);
    if (node === undefined) throw new Error('volt2: the page has no node #' + number);
    return node;
  }

  function contentOf(element) {
    return element.tagName === 'TEMPLATE' ? element.content : element; // a template's children are its content
  }

  function patch(message) {
    if (message.r || !mount) {
      const element = mountElement();
      if (element !== mount) {
        mount = element;
        listening = new Set();
      }
      nodes.clear();
      mount.replaceChildren();
      nodes.set(0, mount);
    }
    if (message.f) dispatcher = fromWire(message.f);
    for (const warning of message.w || []) console.warn('volt2: ' + warning);
    const active = document.activeElement;
    const selection = selectionOf(active);
    const live = [];
    const selects = new Set();
    patching = true;
    try {
      for (const step of message.p) {
        const select = selectAround(run(step, live));
        if (select) selects.add(select);
      }
      // A select shows the option that matches its value, which can be another option once
      // its options change: its value is applied again.
      for (const select of selects) {
        const value = select.getAttribute('value');
        if (value !== null) live.push([select, 'value', value]);
      }
      for (const change of live) setLive(change[0], change[1], change[2]);
      for (const number of message.x) forget(number);
      // A move loses the focus; arrange avoids moving the focused element, but if it had
      // to, the focus and the selection come back.
      if (active && active !== document.activeElement && active.isConnected) {
        active.focus({ preventScroll: true });
        if (selection) {
          try {
            active.setSelectionRange(selection[0], selection[1], selection[2]);
          } catch (e) {}
        }
      }
    } finally {
      patching = false;
    }
    return undefined;
  }

  function selectionOf(element) {
    try {
      if (element && typeof element.selectionStart === 'number') {
        return [element.selectionStart, element.selectionEnd, element.selectionDirection];
      }
    } catch (e) {} // some input types have no selection
    return null;
  }

  function run(step, live) {
    const node = nodeNumbered(step[1]);
    switch (step[0]) {
      case 'o':
        arrange(contentOf(node), step[2].map(function (child) { return make(child, live); }));
        break;
      case 'a':
        setAttribute(node, step[2], step[3], live);
        break;
      case 't':
        node.nodeValue = step[2];
        break;
      case 'e':
        setHandlers(node, step[2]);
        break;
      default:
        throw new Error('volt2: unknown patch step ' + step[0]);
    }
    return node;
  }

  // The select a changed node belongs to: the select itself, or one of its options.
  function selectAround(node) {
    for (let element = node.nodeType === 1 ? node : node.parentNode; element && element.nodeType === 1; element = element.parentNode) {
      if (element.tagName === 'SELECT') return element;
      if (element.tagName !== 'OPTION' && element.tagName !== 'OPTGROUP') return null;
    }
    return null;
  }

  // An existing node by number, or a new one with its children.
  function make(child, live) {
    if (typeof child === 'number') return nodeNumbered(child);
    let node;
    if ('x' in child) {
      node = document.createTextNode(child.x);
    } else {
      node = document.createElement(child.t);
      for (const pair of child.a) setAttribute(node, pair[0], pair[1], live);
      if (child.e) setHandlers(node, child.e);
      const content = contentOf(node);
      for (const grandchild of child.c) content.appendChild(make(grandchild, live));
    }
    nodes.set(child.n, node);
    numberOf.set(node, child.n);
    return node;
  }

  // Makes `wanted` the children of `content`, in that order, moving as few nodes as
  // possible: a move loses focus, selection and scroll positions, and reloads iframes.
  // Anything else goes: volt2 owns everything inside #volt2, which is what lets C++ know
  // exactly what is on the page.
  function arrange(content, wanted) {
    const keep = new Set(wanted);
    for (let child = content.firstChild; child; ) {
      const next = child.nextSibling;
      if (!keep.has(child)) content.removeChild(child);
      child = next;
    }
    const position = new Map();
    let index = 0;
    for (let child = content.firstChild; child; child = child.nextSibling) position.set(child, index++);
    const stay = staying(wanted, position);
    let before = null;
    for (let k = wanted.length - 1; k >= 0; k--) {
      if (!stay[k]) content.insertBefore(wanted[k], before);
      before = wanted[k];
    }
  }

  // Which wanted nodes stay put: the longest run already in order, so the fewest move. The
  // one holding the focus stays whenever it is already there, even if more must move.
  function staying(wanted, position) {
    const stay = new Array(wanted.length).fill(false);
    const active = document.activeElement;
    let pinned = -1;
    if (active && active !== document.body) {
      for (let k = 0; k < wanted.length && pinned < 0; k++) {
        if (position.has(wanted[k]) && wanted[k].contains(active)) pinned = k;
      }
    }
    if (pinned < 0) {
      longestInOrder(wanted, position, 0, wanted.length, -1, Infinity, stay);
    } else {
      const at = position.get(wanted[pinned]);
      stay[pinned] = true;
      longestInOrder(wanted, position, 0, pinned, -1, at, stay);
      longestInOrder(wanted, position, pinned + 1, wanted.length, at, Infinity, stay);
    }
    return stay;
  }

  // Marks the longest increasing run of current positions among wanted[from..to) that lie
  // between low and high.
  function longestInOrder(wanted, position, from, to, low, high, stay) {
    const ks = [];
    const ps = [];
    for (let k = from; k < to; k++) {
      const p = position.get(wanted[k]);
      if (p !== undefined && p > low && p < high) {
        ks.push(k);
        ps.push(p);
      }
    }
    const tails = []; // tails[length - 1]: the run of that length ending lowest
    const previous = new Array(ks.length);
    for (let j = 0; j < ks.length; j++) {
      let lo = 0;
      let hi = tails.length;
      while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (ps[tails[mid]] < ps[j]) lo = mid + 1;
        else hi = mid;
      }
      previous[j] = lo > 0 ? tails[lo - 1] : -1;
      tails[lo] = j;
    }
    for (let j = tails.length ? tails[tails.length - 1] : -1; j >= 0; j = previous[j]) stay[ks[j]] = true;
  }

  function setAttribute(element, name, value, live) {
    if (value === null) element.removeAttribute(name);
    else element.setAttribute(name, value);
    if (name === 'value' || name === 'checked' || name === 'selected') live.push([element, name, value]);
  }

  // What a form control shows is a property, which typing and clicking change; its
  // attribute is only the starting value. So these are set as properties too, once the
  // patch's elements are in place (a select needs its options first). An unchanged value
  // is left alone, which keeps the caret where it is.
  function setLive(element, name, value) {
    const tag = element.tagName;
    try {
      if (name === 'value' && (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT')) {
        const text = value === null ? '' : value;
        if (element.value !== text) element.value = text;
      } else if (name === 'checked' && tag === 'INPUT') {
        element.checked = value !== null;
      } else if (name === 'selected' && tag === 'OPTION') {
        element.selected = value !== null;
      }
    } catch (e) {
      console.error('volt2: cannot set ' + name + ' on <' + tag.toLowerCase() + '>: ' + e.message);
    }
  }

  function setHandlers(element, types) {
    if (types.length) handled.set(element, types);
    else handled.delete(element);
    for (const type of types) {
      if (listening.has(type)) continue;
      listening.add(type);
      mount.addEventListener(type, deliver, true);
    }
  }

  function forget(number) {
    const node = nodes.get(number);
    if (node === undefined) return;
    nodes.delete(number);
    numberOf.delete(node);
    handled.delete(node);
  }

  // The event as one handler sees it: currentTarget is that handler's element (the real
  // event's is the mount, where the listener is). Everything else, methods included,
  // goes to the real event.
  function eventFor(event, element) {
    return new Proxy(event, {
      get: function (target, key) {
        if (key === 'currentTarget') return element;
        const value = Reflect.get(target, key, target);
        return typeof value === 'function' ? value.bind(target) : value;
      },
      set: function (target, key, value) {
        return Reflect.set(target, key, value, target);
      },
    });
  }

  function deliver(event) {
    if (patching || !dispatcher) return;
    const chain = [];
    const views = [];
    for (let node = event.target; node && node !== mount; node = node.parentNode) {
      const types = handled.get(node);
      if (types && types.indexOf(event.type) >= 0) {
        chain.push(numberOf.get(node));
        views.push(eventFor(event, node));
      }
      if (!event.bubbles) break;
    }
    if (chain.length) dispatcher.apply(null, [JSON.stringify(chain), event.type].concat(views));
  }

  // Runs one operation for C++. Errors are returned, never thrown across the bridge.
  // Releases come first: C++ released those ids before sending, so if the result is one
  // of those objects it must get a fresh id. (The operation itself never uses them.)
  function exec(op) {
    release(op.rel);
    try {
      return { v: toWire(perform(op)) };
    } catch (e) {
      return { error: e instanceof Error ? e.name + ': ' + e.message : String(e) };
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
    api.mode = 'development';
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
    api.mode = 'webassembly';
    transport = function (message) {
      return JSON.parse(dispatch(JSON.stringify(message)));
    };
  }

  function execJson(text) {
    return JSON.stringify(exec(JSON.parse(text)));
  }

  const api = {
    mode: 'not started', // then 'development' or 'webassembly'
    startDev: startDev,
    attachWasm: attachWasm,
    execJson: execJson,
    handleCount: function () {
      return handles.size;
    },
    nodeCount: function () {
      return nodes.size; // DOM nodes volt2 tracks, the mount included
    },
  };
  globalThis.volt2 = api;
})();
