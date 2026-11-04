// StopCoroutine(parent) also stops the child it runs with 'yield return Child()',
// but not a child started separately with StartCoroutine.
class G {
    static int frame = 0, a = 0, b = 0;
    static Coroutine parent;
    static IEnumerator Count(string name) {
        while (true) { if (name == "a") a++; else b++; yield return null; }
    }
    static IEnumerator Parent() {
        StartCoroutine(Count("b"));         // independent
        yield return Count("a");            // nested: belongs to Parent
    }
    static void Main() { parent = StartCoroutine(Parent()); }
    static void Update() {
        frame++;
        if (frame == 5) { StopCoroutine(parent); Console.WriteLine("stop at a=" + a + " b=" + b); }
        if (frame == 10) { Console.WriteLine("later a=" + a + " b=" + b); App.Quit(); }
    }
}
