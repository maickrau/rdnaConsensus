## ribotin

rDNA consensus sequence builder. Input hifi or duplex, and optionally ultralong ONT. Extracts rDNA-specific reads based on k-mer matches to a reference rDNA sequence or based on a [verkko](https://github.com/marbl/verkko) assembly, builds a DBG out of them, extracts the most covered path as a consensus and bubbles as variants. Optionally assembles highly abundant rDNA morphs using the ultralong ONT reads.

#### Compilation

- `git clone https://github.com/maickrau/ribotin.git`
- `git submodule update --init --recursive`
- `make all`

Also needs [MBG](https://github.com/maickrau/MBG) version 1.0.13 or more recent.

#### Usage

##### Reference based:

```
bin/ribotin-ref -x human -i hifi_reads1.fa -i hifi_reads2.fq.gz --nano ont_reads.fa -o output_folder
```

This extracts rDNA-specific reads based on k-mer matches to human rDNA, builds a graph and a consensus, and finds variants supported by at least 3 reads. `--nano` is optional, if it is present then ribotin also builds morph consensuses. Results are written to `output_folder`.

##### Verkko based (automatic):

First you must run a whole genome assembly with [verkko](https://github.com/marbl/verkko). Then run:

```
bin/ribotin-verkko -x human -i /path/to/verkko/assembly -o output_folder_prefix
```

This finds the rDNA clusters based on k-mer matches to human rDNA and assembly graph topology, extracts HiFi reads uniquely assigned to each cluster, and for each cluster builds a graph and a consensus and finds variants supported by at least 3 reads and builds morph consensuses. Results are written per cluster to `output_folder_prefix[x]` where `[x]` is the cluster number.

##### Verkko based (manual):

First you must run a whole genome assembly with [verkko](https://github.com/marbl/verkko). Then manually pick the nodes in each rDNA cluster from `assembly.homopolymer-compressed.noseq.gfa`, and save them to files with one cluster per file eg `node_cluster1.txt`, `node_cluster2.txt`, `node_cluster3.txt`. Format of the node cluster files should be eg `utig4-1 utig4-2 utig4-3...` or `utig4-1, utig4-2, utig4-3...` or each node in its own line. Then run:

```
bin/ribotin-verkko -x human -i /path/to/verkko/assembly -o output_folder_prefix -c node_cluster1.txt -c node_cluster2.txt -c node_cluster3.txt
```

This extracts HiFi reads uniquely assigned to each node cluster, and for each cluster builds a graph and a consensus and finds variants supported by at least 3 reads. Results are written per cluster to `output_folder_prefix[x]` where `[x]` is the cluster number. 

##### Nonhumans

For running `ribotin-ref` on nonhumans replace `-x human` with `--approx-morphsize <morphsize> -r path_to_reference_kmers.fa` where `<morphsize>` is the estimated size of a single morph (45000 for human) and `path_to_reference_kmers.fa` is a fasta/fastq file which contains most rDNA k-mers.

For `ribotin-verkko`, replace `-x human` with either `--approx-morphsize <morphsize> --guess-clusters-using-reference path_to_reference_kmers.fa` or `--approx-morphsize <morphsize> -c cluster1.txt -c cluster2.txt` where `<morphsize>` is the estimated size of a single morph (45000 for human) and `path_to_reference_kmers.fa` is a fasta/fastq file which contains most rDNA k-mers and `cluster1.txt cluster2.txt` etc. are manually selected rDNA tangles from the verkko assembly.

You can get the reference k-mers by doing a whole genome assembly with hifi reads using MBG or a similar hifi based assembly tool, and extracting the sequences of the rDNA tangle from the assembly. If you additionally have one complete morph from the same or related species, you can also include `--orient-by-reference previous_reference_single_morph.fa` to have the results in the same orientation (forward / reverse complement) and offset (rotation) as the previous reference.

##### Clustering morphs with ultralong ONT reads

If you have ultralong ONT reads, you can include them to produce consensuses of highly abundant rDNA morphs similar to the CHM13 assembly. For `ribotin-ref`, add the parameter `--nano /path/to/ont/reads.fa` (multiple files may be added with `--nano file1.fa --nano file2.fa` etc). `ribotin-verkko` will automatically check if ONT reads were used in the assembly and use them, and can be overrode with `--do-ul=no`. This will error correct the ultralong ONT reads by aligning them to the variant graph, extract rDNA morphs from the corrected reads, cluster them based on sequence similarity, and compute a consensus for each cluster. This requires [GraphAligner](https://github.com/maickrau/GraphAligner) to be installed.

##### Annotations

You can lift over annotations with the optional parameters `--annotation-reference-fasta` and `--annotation-gff3`, for example `bin/ribotin-verkko ... --annotation-reference-fasta template_seqs/rDNA_one_unit.fasta --annotation-gff3 template_seqs/rDNA_annotation.gff3`. This requires [liftoff](https://github.com/agshumate/Liftoff) to be installed.

#### Output

The output folder will contain several files:

- `nodes.txt`: List of nodes used in this cluster. Only in verkko based mode.
- `hifi_reads.fa`: HiFi or duplex reads used in this cluster.
- `graph.gfa`: de Bruijn graph of the reads.
- `paths.gaf`: Paths of the hifi reads in `graph.gfa`.
- `consensus.fa`: Consensus sequence.
- `consensus_path.gaf`: Path of `consensus.fa` in `graph.gfa`.
- `variants.txt`: A list of variants supported by at least 3 reads. Format is: variant ID, variant path, reference path, variant read support, reference read support, variant sequence, reference sequence.
- `variant-graph.gfa`: `graph.gfa` filtered only to the consensus path and the variant paths in `variants.txt`.
- `variants.vcf`: A list of variants supported by at least 3 reads. Variant IDs match `variants.txt`
- `annotation.gff3`: Annotations lifted over from a previous reference. Only if using parameters `--annotation-reference-fasta` and `--annotation-gff3`

The following files are created when ultralong ONT reads are included:

- `ont-alns.gaf`: Aligned paths of ultralong ONT reads to `variant-graph.gfa`.
- `loops.fa`: A list of individual rDNA morphs found in the ultralong ONT reads.
- `morphs.fa`: A list of rDNA morph consensuses and their abundances.
- `morphs.gaf`: The paths of the rDNA morph consensuses in `variant-graph.gfa`.
- `morphgraph.gfa`: A graph describing how the morph consensuses connect to each others.
- `readpaths-morphgraph.gaf`: Paths of the ONT reads in `morphgraph.gfa`. Only shows reads which are assigned to complete morphs.
