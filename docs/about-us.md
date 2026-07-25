# About Us

OS-PI-is-cool was developed by two students with a shared interest in operating systems, low-level programming, and computer architecture.

## Authors

### Veer Kakar

University of Pennsylvania, Class of 2028  
BSE + MSE in Computer Science, Systems Concentration  
Operating Systems TA

### Matthew Karounos

Penn State, Class of 2027

## Collaboration

The project was built through close technical collaboration. We jointly discussed and designed the major kernel subsystems, reviewed architecture decisions together, and both contributed across the codebase rather than dividing the project into isolated ownership areas.

This collaboration model was especially important because many features cut across subsystem boundaries. For example, `fork`, `exec`, copy-on-write memory, ELF loading, signals, file descriptors, pipes, process groups, and shell behavior all interact through the scheduler, virtual memory system, filesystem, trap path, and userspace runtime.

As a result, we both developed a broad understanding of the full operating-system stack instead of focusing on only one layer.

## Future Additions

This page is a stub for a deeper writeup later, including more background on project motivation, division of work, debugging stories, and major design decisions.
