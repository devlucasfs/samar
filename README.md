# Samar Build Language

Samar is a build language created from scratch in a single night. It was designed to be portable and simple, enabling developers to build programs and upload binaries to microcontrollers (MCUs) without concerns about operating systems or architectures.

Many build systems tie the developer to a specific toolchain, operating system or processor family. Samar takes the opposite approach: it aims to be compatible with as many devices as possible, while offering a better experience when developing a build system. The same build description can therefore be reused across platforms, with only the platform-specific details declared where they belong.

## Shell integration

Samar is inspired by Perl, and it adopts one of Perl's most practical conventions: any command enclosed in backticks is executed by the system shell. On Linux, macOS and FreeBSD, the command is run as a shell script (`.sh`); on Windows, it is run as a batch script (`.bat`).

```perl
`echo "Hello, world!"`
```

This allows a build description to call any tool already available on the host system, such as compilers, linkers, flashing utilities or ordinary shell commands, without requiring a dedicated wrapper for each of them.

# Getting Started

To begin writing Samar code, you should first understand three of its core principles: **rules**, **dependencies** and **runners**. Each of them is described below, with an example. A complete, working build that combines all three is presented at the end of this document.

### Rules

A rule is similar to a function, but it has a specific purpose: it is executed when a required dependency is not installed on the system. Rather than letting the build fail with an obscure error, a rule gives you the opportunity to handle the situation in a clear and helpful way.

Each rule has a name, and a dependency refers to it by that name (see [Dependencies](#dependencies)). Consider the following rule, named `package`:

```perl
rule package {
  die "{@} is not installed, but it was declared in the Sake as a `package`.\n"
      "Try installing it with:\n~> e.g. sudo pacman -S {@}"
}
```

When a dependency that points to this rule is missing, execution is directed to it. Within the rule, you may do one of several things:

- provide recommendations to the user, such as the command needed to install the missing package;
- install the package automatically, when that is appropriate for your project;
- simply report that the dependency must be installed before proceeding, as the example above does with `die`.

Inside a rule, the name of the missing dependency is available as the placeholder `{@}`. In the example above, if `ld` were missing, `{@}` would be replaced by `ld` in the resulting message. This makes it possible to write a single rule that covers every dependency that refers to it.

### Dependencies

Dependencies are the tools and packages that must be present on the system for a program to be built. They are declared inside an architecture namespace, so that each architecture and operating system can list exactly what it requires. A toolchain for `x86_64` on Linux, for instance, does not need to match one for a microcontroller target.

Each entry has the form `name => rule`: the name on the left is the dependency itself, and the name on the right is the rule that will be executed if that dependency is not found. In the example below, the `x86_64::linux` namespace declares that both the assembler (`as`) and the linker (`ld`) are required, and that the `package` rule shown earlier must run if either of them is missing:

```perl
architecture x86_64

x86_64::linux {
    dependencies {
        as => package
        ld => package
    }
}
```

Because the rule is chosen per dependency, different dependencies may be handled differently. A missing compiler could trigger one rule, while a missing optional utility triggers another.

The `dependencies` field is overridable, so a more specific namespace may replace the list defined for a more general one when a target requires different tools.

### Runners

Runners are global functions that can be invoked from code written in an external language, such as C, C++, Go or Rust. They act as the bridge between those languages and the build description, allowing external code to trigger steps defined in Samar.

As with dependencies, a runner must be defined within a namespace, which ties it to the architecture and operating system it is meant for. The following example defines a runner named `runner01`, which simply prints a message using `log`:

```perl
architecture x86_64

x86_64::linux {
    runner runner01 {
        log "Hello, world!"
    }
}
```

The body of a runner may contain any Samar statements, including the backtick commands described earlier.

### Calling a Runner from C

As described above, runners can be invoked from external code. The example below does so from C, using the build file shown after it.

The program loads the build file, passes it an argument and calls a runner:

```c
int main() {
    void *state = __samar_load("./Sake");
    __samar_push_arg(state, "what", "World!");
    __samar_call_runner(state, "abc::xyz::test");
    return 0;
}
```

The build file it loads, `./Sake`, contains:

```perl
architecture abc

abc::xyz {
    dependencies {}

    runner test {
        log "Hello, {what}"
    }
}
```

Running the program prints:

```text
Hello, World!
```

The functions used are:

- `__samar_load(path)` loads and parses the build file at the given path and returns a handle to the resulting state. The remaining functions receive this handle as their first argument.
- `__samar_push_arg(state, name, value)` makes a value available to runners. In this example, it defines `what` with the value `"World!"`, which the runner reads as `{what}`.
- `__samar_call_runner(state, runner)` executes a runner. The runner is identified by its full path, namespace first: `abc::xyz::test` is the runner `test` inside the namespace `abc::xyz`.

An argument pushed with `__samar_push_arg` is only available until the runner is called. It is passed to the runner (here, `test`) for that call and is then removed. If the runner must be called again with the same argument, push it again before the next call.

### Reporting Messages: `die` and `log`

Samar provides two statements for writing messages while a build runs: `die` and `log`. Both write their message to the standard error stream (`stderr`), so diagnostics stay separate from the regular output of the programs being built. They differ in what happens afterwards.

#### `die`

`die` reports a fatal error. It writes its message to `stderr` and then terminates the process with exit status `14`. Nothing after a `die` is executed, and the non-zero status allows scripts and CI pipelines to detect that the build failed.

```perl
die "Something went wrong, the build cannot continue."
```

It is typically used inside rules, as in the `package` rule shown earlier, where a missing dependency makes it impossible to proceed.

#### `log`

`log` writes its message to `stderr` but does not terminate the process, and execution continues normally with the next statement. It is intended for debugging output and warnings, such as reporting which step is running or flagging a condition that deserves attention but is not fatal.

```perl
log "Building for x86_64 [LINUX]..."
```

Both statements accept the placeholders described earlier, such as `{@}` inside a rule or an argument pushed from C (`{what}`).

| Statement | Output stream | Terminates the process | Typical use |
|-----------|---------------|------------------------|-------------|
| `die`     | `stderr`      | Yes, with status `14`  | Fatal errors, such as a missing dependency |
| `log`     | `stderr`      | No                     | Debugging output and warnings |

# A Complete Example

The following build brings the three principles together. It declares a `package` rule, lists the dependencies required on x86_64 Linux, and defines a runner that assembles and links a program:

```perl
architecture x86_64

rule package {
    die "{@} Isn't installed. But was defined into the Sake as a `package`."
        "Try to install using:\n~> eg. sudo pacman -S {@}"
}

x86_64::linux {
    dependencies {
        when $os -eq "x86_64-linux" {
            as  => package
            ld  => package
            rafael => package
        }
    }

    runner binary {
        when $os -eq "x86_64-linux" {
            log "Building for x86_64 [LINUX]..."

            `as {file} -o {object}`
            `ld {object} -o {elf}`
        }

        log "Build finished!"
    }
}
```

The example can be read from top to bottom:

1. **Architecture.** `architecture x86_64` selects the architecture that the namespaces below belong to.
2. **Rule.** The `package` rule is declared once, at the top level, so that every dependency in the file can refer to it. If a dependency is missing, the rule stops the build with `die` and tells the user which package to install, using `{@}` for the name of the missing dependency.
3. **Namespace.** `x86_64::linux` groups everything that is specific to x86_64 on Linux: its dependencies and its runners.
4. **Conditional blocks.** `when` executes a block only if its condition holds. Here, `$os -eq "x86_64-linux"` compares the `$os` variable with the string `"x86_64-linux"`, so the enclosed dependencies and build steps apply only to that target. Wrapping the dependencies in `when` also means they are checked only on the intended platform.
5. **Dependencies.** The assembler (`as`), the linker (`ld`) and a third entry, `rafael`, are all bound to the `package` rule. If any of them cannot be found, `package` runs and reports the problem.
6. **Runner.** The runner named `binary` logs a message, then executes two shell commands enclosed in backticks: `as` assembles the source file into an object file, and `ld` links that object file into the final executable. The placeholders `{file}`, `{object}` and `{elf}` stand for the source file, the intermediate object file and the output executable, respectively.
7. **Final message.** The last `log` is outside the `when` block, so `Build finished!` is printed regardless of the condition.
