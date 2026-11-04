// Dictionary<K,V>. The same file compiles as real C#, so the output is checked against .NET.
using System;
using System.Collections.Generic;

class Item {
    public string name;
    public Item(string n) { name = n; }
}

class Program {
    static Dictionary<string, int> stock;

    static void Main() {
        // basics
        var ages = new Dictionary<string, int>();
        ages.Add("ala", 7);
        ages["ola"] = 9;
        ages["ala"] = 8;                        // overwrite
        Console.WriteLine("count " + ages.Count + " ala " + ages["ala"] + " ola " + ages["ola"]);
        Console.WriteLine("has ola " + ages.ContainsKey("ola") + " has ela " + ages.ContainsKey("e" + "la") + " has value 9 " + ages.ContainsValue(9));

        // TryGetValue with out var, out T and an existing variable
        if (ages.TryGetValue("ola", out var a1)) Console.WriteLine("try ola " + a1);
        bool found = ages.TryGetValue("nobody", out int a2);
        Console.WriteLine("try nobody " + found + " " + a2);
        int a3 = -1;
        ages.TryGetValue("ala", out a3);
        Console.WriteLine("try ala " + a3);

        // compound assignment through the indexer
        ages["ola"] += 10;
        ages["ola"]++;
        Console.WriteLine("ola now " + ages["ola"]);

        // insertion order in foreach, also after Remove + Add
        var order = new Dictionary<string, int> { { "c", 3 }, { "a", 1 }, { "b", 2 } };
        string s = "";
        foreach (var kv in order) s += kv.Key + "=" + kv.Value + " ";
        Console.WriteLine("order " + s);
        Console.WriteLine("remove a " + order.Remove("a") + " again " + order.Remove("a") + " count " + order.Count);
        order["d"] = 4;
        s = "";
        foreach (KeyValuePair<string, int> kv in order) s += kv.Key + kv.Value;
        Console.WriteLine("after " + s);

        // Keys / Values
        int sum = 0;
        foreach (int v in order.Values) sum += v;
        string keys = "";
        foreach (string k in order.Keys) keys += k;
        Console.WriteLine("values sum " + sum + " keys " + keys + " keys count " + order.Keys.Count);

        // int keys, object values, word counting
        var byId = new Dictionary<int, Item>();
        for (int i = 0; i < 5; i++) byId[i * 10] = new Item("item" + i);
        Console.WriteLine("id 30 " + byId[30].name + " count " + byId.Count);
        var words = new Dictionary<string, int>();
        foreach (string w in "the cat and the dog and the bird".Split(' ')) {
            if (words.ContainsKey(w)) words[w]++;
            else words[w] = 1;
        }
        string wc = "";
        foreach (var kv in words) wc += kv.Key + ":" + kv.Value + " ";
        Console.WriteLine(wc);

        // char keys, initializer with [k] = v
        var score = new Dictionary<char, int> { ['a'] = 1, ['b'] = 3, ['c'] = 3 };
        int total = 0;
        foreach (char ch in "abcab") total += score[ch];
        Console.WriteLine("score " + total);

        // many entries: growth, removal, rehash
        var big = new Dictionary<int, int>();
        for (int i = 0; i < 5000; i++) big[i] = i * 2;
        for (int i = 0; i < 5000; i += 2) big.Remove(i);
        for (int i = 0; i < 1000; i++) big[i * 7 + 100000] = i;
        int check = 0;
        foreach (var kv in big) check += kv.Key % 1000 + kv.Value % 7;
        Console.WriteLine("big " + big.Count + " check " + check + " " + big[4999] + " " + big.ContainsKey(4998) + " " + big[100007]);

        // static field, Clear
        stock = new Dictionary<string, int>();
        stock["apple"] = 3;
        stock.Clear();
        Console.WriteLine("cleared " + stock.Count + " " + stock.ContainsKey("apple"));
        stock["pear"] = 1;
        foreach (var kv in stock) Console.WriteLine("stock " + kv.Key + " " + kv.Value);
    }
}
