void* __samar_load(char *file);
void __samar_push_arg(void* state, char *a, char*b);
void __samar_call_runner(void* state, char*a);

int main() {
    void *state = __samar_load("./Sake");
    __samar_push_arg(
        state, "what",
        "World!"
    );

    __samar_call_runner(state, "x86_64::linux::test");
    return 0;
}
