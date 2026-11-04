class Node { public int v; public Node next; public int[] data; }
class P {
    static Node keep;
    static void Main() {
        int live = 0;
        for (int round = 0; round < 200; round++) {
            Node head = null;
            for (int i = 0; i < 1000; i++) {
                Node n = new Node(); n.v = i; n.next = head; n.data = new int[8]; head = n;
                string s = "x" + i;
            }
            if (round % 50 == 0) keep = head;
            int c = 0;
            for (Node n = head; n != null; n = n.next) c++;
            live += c;
        }
        int k = 0;
        for (Node n = keep; n != null; n = n.next) k += n.v;
        Console.WriteLine(live);
        Console.WriteLine(k);
    }
}
