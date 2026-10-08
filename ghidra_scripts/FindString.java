// Cherche des chaînes (regex), liste les fonctions qui les référencent, et décompile ces fonctions.
// Args: <regex> <outfile> [maxfuncs]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;
import java.util.regex.*;

public class FindString extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String rx = a[0].startsWith("@") ? new String(java.nio.file.Files.readAllBytes(java.nio.file.Paths.get(a[0].substring(1)))).trim() : a[0];
        Pattern pat = Pattern.compile(rx, Pattern.CASE_INSENSITIVE);
        int max = a.length > 2 ? Integer.parseInt(a[2]) : 15;
        PrintWriter out = new PrintWriter(new FileWriter(a[1]));
        long base = currentProgram.getImageBase().getOffset();
        Map<Function, List<String>> funcs = new LinkedHashMap<>();
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            if (!d.hasStringValue()) continue;
            Object v = d.getValue();
            if (v == null || !pat.matcher(v.toString()).find()) continue;
            out.println("STR rva=0x" + Long.toHexString(d.getAddress().getOffset()-base) + " \"" + v + "\"");
            for (Reference r : getReferencesTo(d.getAddress())) {
                Function f = getFunctionContaining(r.getFromAddress());
                if (f == null) continue;
                out.println("   ref from " + f.getName() + " rva=0x" + Long.toHexString(f.getEntryPoint().getOffset()-base));
                funcs.computeIfAbsent(f, k -> new ArrayList<>()).add(v.toString());
            }
        }
        DecompInterface d = new DecompInterface(); d.openProgram(currentProgram);
        int n = 0;
        for (Function f : funcs.keySet()) {
            if (n++ >= max) break;
            DecompileResults r = d.decompileFunction(f, 60, monitor);
            out.println("\n===== " + f.getName() + " rva=0x" + Long.toHexString(f.getEntryPoint().getOffset()-base) + " strings=" + funcs.get(f));
            out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "decompile failed");
        }
        out.close();
    }
}
