# LuaPP

LuaPP (as-in Lua++) is a type-safe, separetely-compiled, extensible Lua interpreter
written in modern C++. Its main intent, before anything else, is to add type
safety to a Lua interpreter. Type-safety here means that calling a C++ function
from Lua should not require the programmer to explicitly fetch the arguments
from a stack using casts. Instead, the interpreter itself should check that
the Lua arguments correspond to the expected C++ arguments, and call the required
function with all its parameters already on the C++ stack.

As an illustration, this is how one would write a C++ function to be called from
Lua, using the standard Lua interpreter:

```cpp
// Function signature: int -> const char* -> int -> double -> void
int fn(lua_State* L) {
    // Get parameters from the stack. Error-prone as one can typo the type of
the argument, or its position in the list
    int a = luaL_checkinteger(L, 1);
    const char* b = luaL_checkstring(L, 2);
    int c = luaL_checkinteger(L, 3);
    double d = luaL_checknumber(L, 4);

    // Do stuff

    // Explicitly return 0 to indicate that there are no values returned.
    return 0;
}
```

This is how to do it using LuaPP:

```cpp
void fn(int a, const char* b, int c, double d) {
    // Do stuff
}
```

# Related Work

This interpreter is heavily based on Norman Ramsay's LuaML interpreter written
in OCaml. Both [\[1\]](http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.368.334&rep=rep1&type=pdf) and [\[2\]](https://www.cs.tufts.edu/~nr/pubs/maniaws.pdf) present the main ideas that are 
used in LuaPP. https://github.com/nrnrnr/qc-- is an example of a tool that uses LuaML.
