// Graph actions -> code changes (tests/graph_edit.txt; expected: tests/expected/graph_edit*.txt)
class Edit {
    static int hp = 10;

    static int Twice(int v) => v * 2;

    static void Main() {
        int a = 2 * 3;
        if (hp > 5) hp--;
        while (hp > 100) ;
        for (int i = 0; i < 2; i++) a += i;
        switch (a % 2) {
            case 0: a++; break;
            default: break;
        }
        Console.WriteLine(a + " " + hp + " " + Twice(a));
    }
}
