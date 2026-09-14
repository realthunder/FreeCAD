# The variant Link: logic in one object, data in another

**[recorded 2026-09-12, the user's idea; not sized, not started;
parallel to docs/ProxyChain.md]**

The user's statement, kept as the record:

> The variant Link is ultimately asking for one thing: an efficient way
> to separate logic and data, so that the same logic in an object can
> run using another's data, both read and write.  My idea is to hook
> into each Property class at runtime to check for redirected
> setValue / getValue, which is not quite possible today except by
> manually changing all properties, since there is no single call
> signature of set/getValue().  Let's work on that another time.

## What it asks for

A Link today shows another object's result; a variant Link would RUN
the other object's logic -- its `execute`, its hooks, its expressions
-- against the Link's own property values, reading and writing them,
without copying the object.  `App::Link`'s copy-on-change
(`LinkCopyOnChange`: Enabled / Owned / Tracking) is the copy-based
answer this fork ships: it instantiates the linked tree so the copy
carries its own parameters and tracks the source.  The variant Link
wants the same effect with one logic object and N data objects.

## The blocker, as stated

Redirection at the Property level: while the logic object's code
runs for a given variant, every read and write of its properties
should land on the variant's properties.  There is no single
`getValue`/`setValue` signature across the Property classes
(`PropertyFloat::getValue() -> double`, `PropertyLinkSub::getValue()
-> DocumentObject*` plus the sub-names, `PropertySheet` ...), so a
generic hook cannot be installed once; touching every class by hand
is the cost the idea stalls on.

## Directions to weigh when it is taken up

Noted, not chosen:

- **Redirect at the container, not the property.**  The reads the
  logic makes go through `PropertyContainer::getPropertyByName`, the
  Python wrapper's `_getattr`/`_setattr`, and the expression engine's
  identifier resolution.  A "data context" pushed for the duration of
  a hook call (the logic object's `execute` running under variant V)
  could make those three lookups answer with V's property of the same
  name.  C++ code holding a member reference (`this->Length`) is not
  covered -- which is why the idea reaches for the property itself.
- **Generate the per-class redirection.**  Since the property classes
  are a closed list in the tree, the per-class `setValue`/`getValue`
  forwarding could be generated (cog, the same habit as
  docs/ProxyChain.md sec 3) from a table of the signatures rather than
  written by hand; the manual cost is the table, once.
- **A `Property::redirect(Property*)` base hook plus per-class
  forwarding** is the manual version of the same thing; the generator
  above is how to afford it.
- **The proxy chain first.**  docs/ProxyChain.md gives a data object
  (the feature) a link to a logic object (the sheet or library) and
  calls the logic with the data object as `obj`; that is the read/
  write direction the variant Link wants for the Python-visible half
  already, without any property redirection: the logic reads and
  writes `obj.Length`.  What it does not cover is C++ logic bound to
  member properties, and expressions on the logic object's own
  properties -- the variant Link's residue.

Order: after the proxy chain and the document program; sized then.
