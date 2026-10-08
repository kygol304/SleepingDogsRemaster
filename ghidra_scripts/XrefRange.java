import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
public class XrefRange extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(a[0]));
        long base = currentProgram.getImageBase().getOffset();
        long s = Long.parseLong(a[1].replace("0x",""),16), e = Long.parseLong(a[2].replace("0x",""),16);
        for (long r = s; r < e; r++) {
            for (Reference ref : getReferencesTo(toAddr(base+r))) {
                Function c = getFunctionContaining(ref.getFromAddress());
                out.println("0x"+Long.toHexString(r)+" <- "+Long.toHexString(ref.getFromAddress().getOffset()-base)+" "+(c==null?"?":"fn 0x"+Long.toHexString(c.getEntryPoint().getOffset()-base)));
            }
        }
        out.close();
    }
}
