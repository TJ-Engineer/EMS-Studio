# Interactive UI wireframe

This dependency-free prototype makes the shared CAD Studio and EM Studio workflow visible before the native Qt 6/QML shell is available. It contains presentation state and representative engineering data only; document mutations, evaluation, meshing, and simulation remain owned by the compiled engine modules.

From the repository root, run:

```sh
python3 -m http.server 8080 --directory apps/ui_wireframe
```

Then open <http://localhost:8080>. You can switch studios, select model or study entities, toggle overlays, open the command palette with `Ctrl+K`, and exercise the simulated EM preflight/run flow.

The prototype deliberately uses no external fonts, packages, services, or network resources. Its layout and interaction vocabulary are intended to move into QML; it is not a second application architecture.
