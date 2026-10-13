/* Entry for the doomgeneric engine. The engine itself is not in this tree. */

void doomgeneric_Create(int argc, char **argv);
void doomgeneric_Tick(void);

int main(int argc, char **argv) {
    doomgeneric_Create(argc, argv);
    for (;;) doomgeneric_Tick();
    return 0;
}
