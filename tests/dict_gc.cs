class G {
    static void Main() {
        var keep = new Dictionary<string, List<string>>();
        int total = 0;
        for (int i = 0; i < 3000; i++) {
            var d = new Dictionary<string, string>();
            for (int j = 0; j < 30; j++) d["k" + j] = "v" + i + "_" + j;
            for (int j = 0; j < 30; j += 3) d.Remove("k" + j);
            total += d.Count;
            if (i % 300 == 0) keep["d" + i] = new List<string>(d.Values);
        }
        int kept = 0;
        foreach (var kv in keep) kept += kv.Value.Count;
        Console.WriteLine(total + " " + keep.Count + " " + kept + " " + keep["d2700"][0]);
    }
}
