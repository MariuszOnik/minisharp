class G {
    static int finished = 0, frame = 0;
    static IEnumerator Worker(int id) {
        var junk = new List<string>();
        for (int i = 0; i < 3; i++) { junk.Add("w" + id + "_" + i); yield return null; }
        if (junk[2] == "w" + id + "_2") finished++;
    }
    static void Update() {
        frame++;
        for (int i = 0; i < 200; i++) StartCoroutine(Worker(frame * 1000 + i));
        if (frame == 300) { Console.WriteLine("finished " + finished); App.Quit(); }
    }
}
