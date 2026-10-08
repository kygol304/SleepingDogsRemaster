// Décompile les fonctions aux RVAs données. Args: <outfile> <rva1> [rva2...]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class Decomp extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(a[0]));
        long base = currentProgram.getImageBase().getOffset();
        DecompInterface d = new DecompInterface(); d.openProgram(currentProgram);
        for (int i = 1; i < a.length; i++) {
            long rva = Long.parseLong(a[i].replace("0x",""), 16);
            Function f = getFunctionContaining(toAddr(base + rva));
            if (f == null) { out.println("no function at 0x" + Long.toHexString(rva)); continue; }
            DecompileResults r = d.decompileFunction(f, 120, monitor);
            out.println("===== " + f.getName() + " rva=0x" + Long.toHexString(f.getEntryPoint().getOffset()-base));
            out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "decompile failed");
        }
        out.close();
    }
}
