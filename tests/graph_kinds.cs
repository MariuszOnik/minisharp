// Every statement kind as a graph node (tests/expected/graph_kinds.txt), and the @layout comment.
class Kinds {
    static int total;

    static int Sum(List<int> xs, int limit) {
        int s = 0, n = 0;
        foreach (var x in xs) {
            if (x < 0) continue;
            else if (x > limit) break;
            s += x;
        }
        for (int i = 0; i < xs.Count; i++) n++;
        while (n > 0) { n--; }
        do n++; while (n < 3);
        switch (s % 3) {
            case 0: total += s; break;
            case 1:
            case 2: total -= Mathf.Max(s, limit * 2 + (n - 1) * 4); break;
            default: break;
        }
        { int t = s; }
        ;
        return s > 100 ? 100 : s;
    }

    static IEnumerator Blink() {
        while (true) {
            yield return new WaitForSeconds(0.5f);
            if (total > 10) yield break;
        }
    }

    int Area() => total * 2;

    static void Main() {
        var list = new List<int> { 1, -2, 3, 40 };
        Console.WriteLine(Sum(list, 10) + " " + total);
    }
}
/* @layout
Kinds.Main 0:var:200,10 1:call:-5,-6 1.a0=open
Kinds.Sum 1:each:50,60 2:if:0,0 5.c1_0.a0=closed 8.a0=open
*/
