using System.Collections.Generic;
class P {
    static void Main() {
        var keep = new List<List<string>>();
        int total = 0;
        for (int i = 0; i < 20000; i++) {
            var l = new List<string>();
            for (int j = 0; j < 20; j++) l.Add("s" + j);
            total += l.Count;
            if (i % 1000 == 0) keep.Add(l);
        }
        int kept = 0;
        foreach (var l in keep) kept += l[19].Length;
        Console.WriteLine(total + " " + keep.Count + " " + kept);
    }
}
