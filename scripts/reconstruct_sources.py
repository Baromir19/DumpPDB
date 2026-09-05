#!/usr/bin/env python3
"""Render text templates with a small inline DSL.

The DSL is used to reconstruct per-type source snippets. A template is a
plain-text file such as ``reconstruction_type.example.txt``; it is fed a JSON
object with the data (e.g. ``{"TYPE": {"NAME": "Actor", ...}}``) and the result
is the rendered text.

DSL overview
------------

* Plain text lines are kept as-is and are the only thing that ends up in the
  output.
* ``<path>``          - a variable reference resolved against the data.
* ``\\<path>``         - "mirrored" (escaped) variable: emitted literally as
  ``<path>`` as plain text instead of being resolved.
* ``!IF <path>`` ... ``!END`` - evaluates ``<path>``; the block is emitted only
  when the value is truthy (not ``None``, not ``0``, not an empty container).
* ``!FOR <var> IN <path>`` ... ``!END`` - iterates over the list at ``<path>``;
  each element is bound to ``<var>`` (which may be a scalar or an object
  addressable as ``<var.field>``) and the block body is rendered per element.
* Lines starting with ``#`` are comments and are ignored.

Example::

    !FOR <PREFIX> IN <TYPE.PREFIXES>
    MACRO_<PREFIX>(<TYPE.NAME>)
    !END

    \\hidden<TYPE.SECRET>   <-- emitted literally (mirrored, not resolved)
"""

import argparse
import json
import sys


# ---------------------------------------------------------------------------
# Node types
# ---------------------------------------------------------------------------

class _Node:
    """Base class for a parsed template node."""

    def render(self, context):
        raise NotImplementedError


class _Text(_Node):
    """A text line that may contain ``<variable>`` tokens."""

    __slots__ = ("text",)

    def __init__(self, text):
        self.text = text

    def render(self, context):
        return _resolve_line(self.text, context) + "\n"


class _If(_Node):
    """A conditional block: ``!IF <path> ... !END``."""

    __slots__ = ("path", "body")

    def __init__(self, path, body):
        self.path = path
        self.body = body

    def render(self, context):
        if _truthy(_resolve(self.path, context, allow_missing=True)):
            return _render(self.body, context)
        return ""


class _For(_Node):
    """A list loop: ``!FOR <var> IN <path> ... !END``."""

    __slots__ = ("var", "path", "body")

    def __init__(self, var, path, body):
        self.var = var
        self.path = path
        self.body = body

    def render(self, context):
        seq = _resolve(self.path, context, allow_missing=True)
        if seq is None:
            return ""
        if not _is_iterable(seq):
            seq = (seq,)

        parts = []
        for element in seq:
            scope = dict(context.scope)
            scope[self.var] = element
            parts.append(_render(self.body, _Context(context.root, scope)))
        return "".join(parts)


# ---------------------------------------------------------------------------
# Resolution context
# ---------------------------------------------------------------------------

class _Context:
    """Resolution context: the root data plus a flat variable scope."""

    __slots__ = ("root", "scope")

    def __init__(self, root, scope=None):
        self.root = root
        self.scope = scope or {}


# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------

def _is_iterable(value):
    if isinstance(value, (str, bytes)):
        return False
    try:
        iter(value)
        return True
    except TypeError:
        return False


def _truthy(value):
    """DLS truthiness: everything is truthy except None, 0, False and empties."""
    if value is None:
        return False
    if value is False or value == 0:
        return False
    if isinstance(value, (str, list, tuple, dict, set)) and len(value) == 0:
        return False
    return True


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

def _strip_var(text):
    """Turn ``<some.path>`` or ``some.path`` into ``some.path``."""
    text = text.strip()
    if text.startswith("<") and text.endswith(">"):
        return text[1:-1].strip()
    return text


def _parse(lines, index, depth=0):
    """Parse ``lines`` starting at ``index``.

    Returns ``(nodes, next_index)`` where ``next_index`` points just past the
    closing ``!END`` (or at ``len(lines)`` for the top-level call). ``depth``
    is the current block-nesting level and is used only to reject an
    unmatched ``!END``.
    """
    nodes = []

    while index < len(lines):
        stripped = lines[index].strip()

        if stripped != "" and stripped.startswith("#"):
            index += 1
            continue

        if stripped.startswith("!"):
            keyword, _, argument = stripped.partition(" ")

            if keyword == "!END":
                if depth == 0:
                    raise SyntaxError(f"unexpected !END at line {index + 1}")
                return nodes, index + 1

            if keyword == "!IF":
                if not argument.strip():
                    raise SyntaxError(f"!IF without a path at line {index + 1}")
                body, index = _parse(lines, index + 1, depth + 1)
                nodes.append(_If(_strip_var(argument), body))
                continue

            if keyword == "!FOR":
                if argument.count(" IN ") != 1:
                    raise SyntaxError(
                        f"!FOR must look like '!FOR <var> IN <path>' "
                        f"(line {index + 1})"
                    )
                var_part, _, path_part = argument.partition(" IN ")
                var = _strip_var(var_part)
                if not var:
                    raise SyntaxError(f"!FOR without a variable at line {index + 1}")
                path = _strip_var(path_part)
                if not path:
                    raise SyntaxError(f"!FOR without a list path at line {index + 1}")
                body, index = _parse(lines, index + 1, depth + 1)
                nodes.append(_For(var, path, body))
                continue

            raise SyntaxError(f"unknown directive '{keyword}' at line {index + 1}")

        # Plain (or variable-bearing) line.
        nodes.append(_Text(lines[index]))
        index += 1

    return nodes, index
# ---------------------------------------------------------------------------
# Resolution & rendering
# ---------------------------------------------------------------------------

def _resolve(path, context, allow_missing=False):
    """Resolve dotted ``path`` against the current scope and the root data."""
    segments = [s for s in (c.strip() for c in path.split(".")) if s]
    if not segments:
        if allow_missing:
            return None
        raise KeyError(f"empty variable token '<{path}>'")

    first, rest = segments[0], segments[1:]

    if first in context.scope:
        current = context.scope[first]
    elif isinstance(context.root, dict) and first in context.root:
        current = context.root[first]
    else:
        if allow_missing:
            return None
        raise KeyError(
            f"unknown variable '<{first}>' "
            f"(available: {sorted(context.scope)} + root keys)"
        )

    for seg in rest:
        if isinstance(current, dict) and seg in current:
            current = current[seg]
        elif allow_missing:
            return None
        else:
            current_type = type(current).__name__
            raise KeyError(f"cannot resolve '<{seg}>' on {current_type} value")

    return current


def _stringify(value):
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (dict, list)):
        return json.dumps(value, ensure_ascii=False)
    return str(value)


def _resolve_line(text, context):
    """Render a single line, resolving ``<path>`` tokens and mirroring
    ``\\<path>`` (and ``\\<path\\>``) as literal text."""
    output = []
    i, n = 0, len(text)

    while i < n:
        ch = text[i]

        if ch == "\\" and i + 1 < n and text[i + 1] == "<":
            end = text.find(">", i + 2)
            if end == -1:  # no closing '>', keep the rest verbatim
                output.append(text[i:])
                break
            segment = text[i:end + 1]  # e.g. "\<X>" or "\<X\>"
            if segment.endswith("\\>"):
                segment = segment[:-2] + ">"  # drop trailing backslash-close
            if segment.startswith("\\"):
                segment = segment[1:]  # drop the leading escape backslash
            output.append(segment)
            i = end + 1
            continue

        if ch == "<":
            end = text.find(">", i + 1)
            if end == -1:  # no closing '>', keep the rest verbatim
                output.append(text[i:])
                break
            path = text[i + 1:end].strip()
            output.append(_stringify(_resolve(path, context)))
            i = end + 1
            continue

        output.append(ch)
        i += 1

    return "".join(output)


def _render(nodes, context):
    return "".join(node.render(context) for node in nodes)


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def reconstruct(template_text, data=None):
    """Render ``template_text`` against ``data``.

    Args:
        template_text: Raw template string containing the DSL.
        data: The JSON-like object providing the values for ``<path>`` tokens.

    Returns:
        The fully rendered text.
    """
    data = data if data is not None else {}
    lines = template_text.split("\n")
    nodes, index = _parse(lines, 0)
    if index < len(lines):
        stripped = lines[index].strip()
        if stripped.startswith("!"):
            raise SyntaxError(
                f"missing !END for block opened at line {index + 1}"
            )
    context = _Context(data)
    result = _render(nodes, context)
    return result[: -1] if result.endswith("\n") else result


def reconstruct_from_file(template_path, data=None):
    """Read ``template_path`` and render it with ``reconstruct``."""
    with open(template_path, "r", encoding="utf-8") as handle:
        return reconstruct(handle.read(), data)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Render a reconstruction template (DSL) with JSON data.",
    )
    parser.add_argument(
        "--template",
        required=True,
        help="Path to the template file (e.g. reconstruction_type.example.txt)",
    )
    args = parser.parse_args(argv)

    data = {
        "TYPE": {"NAME": "Actor", "PREFIXES": ["PREF_1", "PREF_2"], "SUFFIXES": ["SUF_DEF"], "OBJECT": "struct Actor {}"},
    }

    result = reconstruct_from_file(args.template, data)

    print(result)

    return 0


if __name__ == "__main__":
    sys.exit(main())