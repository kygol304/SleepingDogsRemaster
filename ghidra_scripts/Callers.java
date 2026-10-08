// Liste et décompile les fonctions qui appellent une RVA donnée. Args: <outfile> <rva> [max]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class Callers extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(a[0]));
        long base = currentProgram.getImageBase().getOffset();
        long rva = Long.parseLong(a[1].replace("0x",""), 16);
        int max = a.length > 2 ? Integer.parseInt(a[2]) : 10;
        Set<Function> callers = new LinkedHashSet<>();
        for (Reference r : getReferencesTo(toAddr(base + rva))) {
            Function f = getFunctionContaining(r.getFromAddress());
            if (f != null) callers.add(f);
        }
        out.println("callers: " + callers.size());
        DecompInterface d = new DecompInterface(); d.openProgram(currentProgram);
        int n = 0;
        for (Function f : callers) {
            out.println("CALLER " + f.getName() + " rva=0x" + Long.toHexString(f.getEntryPoint().getOffset()-base));
        }
        for (Function f : callers) {
            if (n++ >= max) break;
            DecompileResults res = d.decompileFunction(f, 120, monitor);
            out.println("\n===== " + f.getName() + " rva=0x" + Long.toHexString(f.getEntryPoint().getOffset()-base));
            out.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "decompile failed");
        }
        out.close();
    }
}
