// Trouve un symbole de vtable (sous-chaîne du nom), liste ses entrées et décompile les N premières.
// Args: <outfile> <nom> [nbEntrees] [decompilerMax]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.address.*;
import java.io.*;

public class VTable extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(a[0]));
        String want = a[1];
        int n = a.length > 2 ? Integer.parseInt(a[2]) : 20;
        int dmax = a.length > 3 ? Integer.parseInt(a[3]) : n;
        long base = currentProgram.getImageBase().getOffset();
        DecompInterface d = new DecompInterface(); d.openProgram(currentProgram);
        SymbolIterator it = currentProgram.getSymbolTable().getAllSymbols(true);
        while (it.hasNext()) {
            Symbol s = it.next();
            String full = s.getName(true);
            if (!full.contains(want) || !s.getName().equals("vftable")) continue;
            Address va = s.getAddress();
            out.println("VTABLE " + full + " rva=0x" + Long.toHexString(va.getOffset()-base));
            for (int i = 0; i < n; i++) {
                long ptr = getLong(va.add(i * 8L));
                Function f = getFunctionAt(toAddr(ptr));
                out.println(String.format("  [%2d] +0x%03x -> rva=0x%x %s", i, i * 8, ptr - base, f == null ? "?" : f.getName()));
                if (f != null && i < dmax) {
                    DecompileResults r = d.decompileFunction(f, 60, monitor);
                    if (r.decompileCompleted()) out.println(r.getDecompiledFunction().getC());
                }
            }
        }
        out.close();
    }
}
