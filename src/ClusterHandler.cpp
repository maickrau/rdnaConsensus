#include <limits>
#include <queue>
#include <map>
#include <cstdlib>
#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <fstream>
#include <tuple>
#include <cassert>
#include <set>
#include <vector>
#include <algorithm>
#include <sstream>
#include "fastqloader.h"
#include "ClusterHandler.h"

namespace std
{
	template <> struct hash<std::pair<std::string, std::string>>
	{
		size_t operator()(const std::pair<std::string, std::string>& x) const
		{
			return std::hash<std::string>{}(x.first) ^ std::hash<std::string>{}(x.second);
		}
	};
}

std::string revcomp(std::string seq)
{
	std::reverse(seq.begin(), seq.end());
	for (size_t i = 0; i < seq.size(); i++)
	{
		switch(seq[i])
		{
			case 'a':
			case 'A':
				seq[i] = 'T';
				break;
			case 'c':
			case 'C':
				seq[i] = 'G';
				break;
			case 'g':
			case 'G':
				seq[i] = 'C';
				break;
			case 't':
			case 'T':
				seq[i] = 'A';
				break;
			default:
				assert(false);
				break;
		}
	}
	return seq;
}

std::string reverse(std::string node)
{
	if (node[0] == '>')
	{
		node[0] = '<';
	}
	else
	{
		assert(node[0] == '<');
		node[0] = '>';
	}
	return node;
}

std::vector<std::string> reverse(const std::vector<std::string>& path)
{
	std::vector<std::string> bw { path.rbegin(), path.rend() };
	for (size_t i = 0; i < bw.size(); i++)
	{
		bw[i] = reverse(bw[i]);
	}
	return bw;
}

std::vector<std::string> canon(const std::vector<std::string>& path)
{
	auto bw = reverse(path);
	if (bw < path) return bw;
	return path;
}

std::pair<std::string, std::string> canon(const std::string& left, const std::string& right)
{
	auto revleft = reverse(left);
	auto revright = reverse(right);
	if (revright < left) return std::make_pair(revright, revleft);
	if (left < revright) return std::make_pair(left, right);
	if (revleft < right) return std::make_pair(revright, revleft);
	return std::make_pair(left, right);
}

class ReadPath
{
public:
	std::string readName;
	size_t readStart;
	size_t readEnd;
	std::vector<std::string> path;
	size_t pathStartClip;
	size_t pathEndClip;
};

class Variant
{
public:
	Variant() = default;
	std::string name;
	std::vector<std::string> path;
	std::vector<std::string> referencePath;
	size_t coverage;
	size_t referenceCoverage;
	size_t referenceStartPos;
	size_t referenceEndPos;
	std::string variantSeq;
	std::string referenceSeq;
};

class GfaGraph
{
public:
	void loadFromFile(std::string gfaFile)
	{
		std::ifstream file { gfaFile };
		while (file.good())
		{
			std::string line;
			getline(file, line);
			std::stringstream sstr { line };
			std::string test;
			sstr >> test;
			if (test == "S")
			{
				std::string node, seq, coveragetag;
				sstr >> node >> seq >> coveragetag;
				nodeSeqs[node] = seq;
				nodeCoverages[node] = std::stof(coveragetag.substr(5));
			}
			else if (test == "L")
			{
				std::string fromnode;
				std::string tonode;
				std::string fromorient, toorient;
				std::string overlapstr, edgecoveragetag;
				sstr >> fromnode >> fromorient >> tonode >> toorient >> overlapstr >> edgecoveragetag;
				if (fromorient == "+")
				{
					fromnode = ">" + fromnode;
				}
				else
				{
					fromnode = "<" + fromnode;
				}
				if (toorient == "+")
				{
					tonode = ">" + tonode;
				}
				else
				{
					tonode = "<" + tonode;
				}
				overlapstr.pop_back();
				size_t overlap = std::stoull(overlapstr);
				size_t coverage = std::stoull(edgecoveragetag.substr(5));
				edges[fromnode].emplace(tonode, overlap, coverage);
				edges[reverse(tonode)].emplace(reverse(fromnode), overlap, coverage);
			}
		}
	}
	std::unordered_map<std::string, size_t> nodeCoverages;
	std::unordered_map<std::string, std::string> nodeSeqs;
	std::unordered_map<std::string, std::set<std::tuple<std::string, size_t, size_t>>> edges;
};

class Path
{
public:
	Path() :
		nodes(),
		overlaps(),
		leftClip(0),
		rightClip(0)
	{
	}
	std::vector<std::string> nodes;
	std::vector<size_t> overlaps;
	size_t leftClip;
	size_t rightClip;
	std::string getSequence(const std::unordered_map<std::string, std::string>& nodeSeqs) const
	{
		std::string result;
		for (size_t i = 0; i < nodes.size(); i++)
		{
			std::string add;
			add = nodeSeqs.at(nodes[i].substr(1));
			if (nodes[i][0] == '<')
			{
				add = revcomp(add);
			}
			else
			{
				assert(nodes[i][0] == '>');
			}
			if (i > 0) add = add.substr(overlaps[i]);
			result += add;
		}
		result = result.substr(leftClip, result.size() - leftClip - rightClip);
		return result;
	}
};

std::string getSequence(const std::vector<std::string>& nodes, const std::unordered_map<std::string, std::string>& nodeSeqs, const std::unordered_map<std::string, std::set<std::tuple<std::string, size_t, size_t>>>& edges)
{
	std::string result;
	for (size_t i = 0; i < nodes.size(); i++)
	{
		std::string add = nodeSeqs.at(nodes[i].substr(1));
		if (nodes[i][0] == '<')
		{
			add = revcomp(add);
		}
		else
		{
			assert(nodes[i][0] == '>');
		}
		if (i > 0)
		{
			size_t overlap = std::numeric_limits<size_t>::max();
			for (auto edge : edges.at(nodes[i-1]))
			{
				if (std::get<0>(edge) != nodes[i]) continue;
				assert(overlap == std::numeric_limits<size_t>::max());
				overlap = std::get<1>(edge);
			}
			assert(overlap != std::numeric_limits<size_t>::max());
			add = add.substr(overlap);
		}
		result += add;
	}
	return result;
}

std::string getPathGaf(const Path& path, const GfaGraph& graph)
{
	std::string pathseq = path.getSequence(graph.nodeSeqs);
	std::string result = "heavy_path\t";
	result += std::to_string(pathseq.size());
	result += "\t0\t";
	result += std::to_string(pathseq.size());
	result += "\t+\t";
	for (const auto& node : path.nodes)
	{
		result += node;
	}
	result += "\t";
	result += std::to_string(pathseq.size() + path.leftClip + path.rightClip);
	result += "\t";
	result += std::to_string(path.leftClip);
	result += "\t";
	result += std::to_string(pathseq.size() + path.leftClip);
	result += "\t";
	result += std::to_string(pathseq.size());
	result += "\t";
	result += std::to_string(pathseq.size());
	result += "\t60";
	return result;
}

class CoverageComparer
{
public:
	bool operator()(const std::tuple<std::string, std::string, double>& lhs, const std::tuple<std::string, std::string, double>& rhs) const
	{
		return std::get<2>(lhs) < std::get<2>(rhs);
	}
};

Path getHeavyPath(const GfaGraph& graph)
{
	std::string maxCoverageNode;
	for (auto pair : graph.nodeCoverages)
	{
		if (maxCoverageNode == "" || pair.second > graph.nodeCoverages.at(maxCoverageNode))
		{
			maxCoverageNode = pair.first;
		}
	}
	std::unordered_map<std::string, std::pair<std::string, double>> predecessor;
	std::priority_queue<std::tuple<std::string, std::string, double>, std::vector<std::tuple<std::string, std::string, double>>, CoverageComparer> checkStack;
	checkStack.emplace(">" + maxCoverageNode, ">" + maxCoverageNode, 0);
	while (checkStack.size() > 0)
	{
		auto top = checkStack.top();
		checkStack.pop();
		if (graph.edges.count(std::get<0>(top)) == 0) continue;
		if (predecessor.count(std::get<0>(top)) == 1 && predecessor.at(std::get<0>(top)).second >= std::get<2>(top)) continue;
		predecessor[std::get<0>(top)] = std::make_pair(std::get<1>(top), std::get<2>(top));
		for (auto edge : graph.edges.at(std::get<0>(top)))
		{
			double coverage = std::min(graph.nodeCoverages.at(std::get<0>(edge).substr(1)), std::get<2>(edge));
			if (predecessor.count(std::get<0>(edge)) == 1 && predecessor.at(std::get<0>(edge)).second > coverage) continue;
			checkStack.emplace(std::get<0>(edge), std::get<0>(top), coverage);
		}
	}
	Path result;
	result.nodes.emplace_back(">" + maxCoverageNode);
	std::unordered_set<std::string> visited;
	while (true)
	{
		assert(predecessor.count(result.nodes.back()) == 1);
		auto pre = predecessor.at(result.nodes.back());
		if (std::get<0>(pre).substr(1) == maxCoverageNode)
		{
			break;
		}
		assert(visited.count(pre.first) == 0);
		result.nodes.emplace_back(pre.first);
	}
	std::reverse(result.nodes.begin(), result.nodes.end());
	for (size_t i = 0; i < result.nodes.size(); i++)
	{
		std::string prev;
		if (i > 0)
		{
			prev = result.nodes[i-1];
		}
		else
		{
			prev = result.nodes.back();
		}
		assert(graph.edges.count(prev) == 1);
		for (auto edge : graph.edges.at(prev))
		{
			if (std::get<0>(edge) != result.nodes[i]) continue;
			result.overlaps.push_back(std::get<1>(edge));
		}
	}
	assert(result.overlaps.size() == result.nodes.size());
	result.leftClip = 0;
	result.rightClip = result.overlaps[0];
	return result;
}

void writePathSequence(const Path& path, const GfaGraph& graph, std::string outputFile)
{
	std::string pathseq = path.getSequence(graph.nodeSeqs);
	std::ofstream file { outputFile };
	file << ">heavy_path" << std::endl;
	file << pathseq;
}

void writePathGaf(const Path& path, const GfaGraph& graph, std::string outputFile)
{
	std::ofstream file { outputFile };
	file << getPathGaf(path, graph);
}

void runMBG(std::string basePath, std::string readPath, std::string MBGPath, size_t k)
{
	std::string mbgCommand;
	mbgCommand = MBGPath + " -o " + basePath + "/graph.gfa -i " + readPath + " -k " + std::to_string(k) + " -w " + std::to_string(k-30) + " -a 2 -u 3 -r 15000 -R 4000 --error-masking=msat --output-sequence-paths " + basePath + "/paths.gaf --only-local-resolve 1> " + basePath + "/mbg_stdout.txt 2> " + basePath + "/mbg_stderr.txt";
	std::cerr << "MBG command:" << std::endl;
	std::cerr << mbgCommand << std::endl;
	system(mbgCommand.c_str());
}

std::vector<std::string> parsePath(const std::string& pathstr)
{
	std::vector<std::string> result;
	size_t lastStart = 0;
	for (size_t i = 1; i < pathstr.size(); i++)
	{
		if (pathstr[i] != '<' && pathstr[i] != '>') continue;
		result.emplace_back(pathstr.substr(lastStart, i - lastStart));
		lastStart = i;
	}
	result.emplace_back(pathstr.substr(lastStart));
	return result;
}

std::vector<ReadPath> loadReadPaths(const std::string& filename)
{
	std::vector<ReadPath> result;
	std::ifstream file { filename };
	while (file.good())
	{
		std::string line;
		getline(file, line);
		if (!file.good()) break;
		std::stringstream sstr { line };
		ReadPath here;
		std::string dummy;
		std::string pathstr;
		size_t pathLength, pathStart, pathEnd;
		sstr >> here.readName >> dummy >> here.readStart >> here.readEnd >> dummy >> pathstr >> pathLength >> pathStart >> pathEnd;
		here.pathStartClip = pathStart;
		here.pathEndClip = pathLength - pathEnd;
		here.path = parsePath(pathstr);
		result.emplace_back(here);
	}
	return result;
}

std::vector<std::string> getReferenceAllele(const Path& heavyPath, const std::string startNode, const std::string endNode)
{
	size_t startIndex = heavyPath.nodes.size();
	size_t endIndex = heavyPath.nodes.size();
	for (size_t i = 0; i < heavyPath.nodes.size(); i++)
	{
		if (i == heavyPath.nodes.size()-1 && heavyPath.nodes[0] == heavyPath.nodes.back()) continue;
		if (heavyPath.nodes[i] == startNode) startIndex = i;
		if (heavyPath.nodes[i] == endNode) endIndex = i;
	}
	assert(startIndex < heavyPath.nodes.size());
	assert(endIndex < heavyPath.nodes.size());
	assert(startIndex != endIndex);
	std::vector<std::string> result;
	if (endIndex > startIndex)
	{
		result.insert(result.end(), heavyPath.nodes.begin() + startIndex, heavyPath.nodes.begin() + endIndex + 1);
	}
	else
	{
		result.insert(result.end(), heavyPath.nodes.begin() + startIndex, heavyPath.nodes.end());
		assert(result.size() >= 1);
		if (heavyPath.nodes[0] == heavyPath.nodes.back())
		{
			assert(result.back() == heavyPath.nodes[0]);
			result.pop_back();
		}
		result.insert(result.end(), heavyPath.nodes.begin(), heavyPath.nodes.begin() + endIndex + 1);
	}
	return result;
}

bool isSubstring(const std::vector<std::string>& small, const std::vector<std::string>& big)
{
	if (small.size() > big.size()) return false;
	for (size_t i = 0; i <= big.size() - small.size(); i++)
	{
		bool match = true;
		for (size_t j = 0; j < small.size(); j++)
		{
			if (big[i+j] != small[j])
			{
				match = false;
				break;
			}
		}
		if (match)
		{
			return true;
		}
	}
	return false;
}

size_t getReferenceCoverage(const std::vector<ReadPath>& readPaths, const std::vector<std::string>& referenceAllele)
{
	size_t result = 0;
	auto reverseRefAllele = reverse(referenceAllele);
	for (const auto& readPath : readPaths)
	{
		if (isSubstring(referenceAllele, readPath.path))
		{
			result += 1;
			continue;
		}
		if (isSubstring(reverseRefAllele, readPath.path))
		{
			result += 1;
			continue;
		}
	}
	return result;
}

std::vector<Variant> getVariants(const GfaGraph& graph, const Path& heavyPath, const std::vector<ReadPath>& readPaths, const size_t minCoverage)
{
	std::unordered_set<std::string> partOfHeavyPath;
	std::unordered_map<std::string, bool> nodeOrientation;
	for (auto node : heavyPath.nodes)
	{
		partOfHeavyPath.insert(node.substr(1));
		nodeOrientation[node.substr(1)] = node[0] == '>';
	}
	std::map<std::vector<std::string>, size_t> bubbleCoverages;
	for (const auto& path : readPaths)
	{
		size_t lastStart = std::numeric_limits<size_t>::max();
		for (size_t i = 0; i < path.path.size(); i++)
		{
			if (partOfHeavyPath.count(path.path[i].substr(1)) == 0) continue;
			if (lastStart != std::numeric_limits<size_t>::max())
			{
				std::vector<std::string> part { path.path.begin() + lastStart, path.path.begin() + i + 1 };
				part = canon(part);
				bubbleCoverages[part] += 1;
			}
			lastStart = i;
		}
	}
	std::vector<Variant> result;
	for (const auto& pair : bubbleCoverages)
	{
		if (pair.second < minCoverage) continue;
		std::vector<std::string> path = pair.first;
		if (nodeOrientation[path[0].substr(1)] != (path[0][0] == '>'))
		{
			path = reverse(path);
		}
		std::vector<std::string> referenceAllele = getReferenceAllele(heavyPath, path[0], path.back());
		if (path == referenceAllele) continue;
		result.emplace_back();
		result.back().path = path;
		result.back().coverage = pair.second;
		result.back().referencePath = referenceAllele;
		result.back().referenceCoverage = getReferenceCoverage(readPaths, result.back().referencePath);
	}
	return result;
}

void writeVariants(const Path& heavyPath, const GfaGraph& graph, const std::vector<Variant>& variants, const std::string& filename)
{
	std::ofstream file { filename };
	for (const auto& variant : variants)
	{
		file << variant.name << "\t";
		for (const auto& node : variant.path)
		{
			file << node;
		}
		file << "\t";
		for (const auto& node : variant.referencePath)
		{
			file << node;
		}
		file << "\t" << variant.coverage << "\t" << variant.referenceCoverage;
		file << "\t" << variant.variantSeq << "\t" << variant.referenceSeq << std::endl;
	}
}

void writeVariantGraph(std::string graphFileName, const Path& heavyPath, const std::vector<Variant>& variants, std::string variantGraphFileName)
{
	std::unordered_set<std::string> allowedNodes;
	std::set<std::pair<std::string, std::string>> allowedEdges;
	for (size_t i = 0; i < heavyPath.nodes.size(); i++)
	{
		std::string prev;
		if (i == 0)
		{
			prev = heavyPath.nodes.back();
		}
		else
		{
			prev = heavyPath.nodes[i-1];
		}
		std::string curr = heavyPath.nodes[i];
		allowedEdges.emplace(prev, curr);
		allowedEdges.emplace(reverse(curr), reverse(prev));
		allowedNodes.emplace(curr.substr(1));
	}
	for (const auto& variant : variants)
	{
		for (size_t i = 0; i < variant.path.size(); i++)
		{
			std::string prev;
			if (i == 0)
			{
				prev = variant.path.back();
			}
			else
			{
				prev = variant.path[i-1];
			}
			std::string curr = variant.path[i];
			allowedEdges.emplace(prev, curr);
			allowedEdges.emplace(reverse(curr), reverse(prev));
			allowedNodes.emplace(curr.substr(1));
		}
	}
	std::ifstream in { graphFileName };
	std::ofstream out { variantGraphFileName };
	while (in.good())
	{
		std::string line;
		getline(in, line);
		std::stringstream sstr { line };
		std::string test;
		sstr >> test;
		if (test == "S")
		{
			std::string node;
			sstr >> node;
			if (allowedNodes.count(node) == 1)
			{
				out << line << std::endl;
			}
		}
		else if (test == "L")
		{
			std::string fromnode, fromorient, tonode, toorient;
			sstr >> fromnode >> fromorient >> tonode >> toorient;
			fromnode = (fromorient == "+" ? ">" : "<") + fromnode;
			tonode = (toorient == "+" ? ">" : "<") + tonode;
			if (allowedEdges.count(std::make_pair(fromnode, tonode)) == 1)
			{
				out << line << std::endl;
			}
		}
	}
}

Path orientPath(const GfaGraph& graph, const Path& rawPath, const std::string& orientReferencePath, size_t k)
{
	Path result = rawPath;
	std::unordered_map<uint64_t, size_t> kmerReferencePosition;
	FastQ::streamFastqFromFile(orientReferencePath, false, [&kmerReferencePosition, k](FastQ& fastq)
	{
		for (size_t i = 0; i < fastq.sequence.size()-k; i++)
		{
			uint64_t hash = std::hash<std::string>{}(fastq.sequence.substr(i, k));
			if (kmerReferencePosition.count(hash) == 1)
			{
				kmerReferencePosition[hash] = std::numeric_limits<size_t>::max();
			}
			else
			{
				kmerReferencePosition[hash] = i;
			}
		}
	});
	{
		std::string pathSeq = result.getSequence(graph.nodeSeqs);
		std::string reverseSeq = revcomp(pathSeq);
		size_t fwMatches = 0;
		size_t bwMatches = 0;
		for (size_t i = 0; i < pathSeq.size() - k; i++)
		{
			uint64_t hash = std::hash<std::string>{}(pathSeq.substr(i, k));
			if (kmerReferencePosition.count(hash) == 1) fwMatches += 1; // no need to check if the hash is unique, it's still valid for orientation
		}
		for (size_t i = 0; i < reverseSeq.size() - k; i++)
		{
			uint64_t hash = std::hash<std::string>{}(reverseSeq.substr(i, k));
			if (kmerReferencePosition.count(hash) == 1) bwMatches += 1; // no need to check if the hash is unique, it's still valid for orientation
		}
		if (fwMatches == 0 && bwMatches == 0)
		{
			std::cerr << "Can't be matched to the reference!" << std::endl;
			return rawPath;
		}
		if (bwMatches > fwMatches)
		{
			std::cerr << "reverse complement" << std::endl;
			std::reverse(result.nodes.begin(), result.nodes.end());
			for (size_t i = 0; i < result.nodes.size(); i++)
			{
				result.nodes[i] = reverse(result.nodes[i]);
			}
			std::reverse(result.overlaps.begin(), result.overlaps.end());
			result.overlaps.insert(result.overlaps.begin(), result.overlaps.back());
			result.overlaps.pop_back();
		}
	}
	std::string pathSeq = result.getSequence(graph.nodeSeqs);
	// todo: this does not work in general due to spurious k-mer hits, but seems to be good enough for now?
	// should instead do chaining between matches and pick leftmost ref pos from them
	std::pair<size_t, size_t> minMatchPos { std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max() };
	for (size_t i = 0; i < pathSeq.size() - k; i++)
	{
		uint64_t hash = std::hash<std::string>{}(pathSeq.substr(i, k));
		if (kmerReferencePosition.count(hash) == 0) continue;
		auto refPos = kmerReferencePosition.at(hash);
		if (refPos == std::numeric_limits<size_t>::max()) continue;
		if (refPos < minMatchPos.second)
		{
			minMatchPos.first = i;
			minMatchPos.second = refPos;
		}
	}
	assert(minMatchPos.first != std::numeric_limits<size_t>::max());
	size_t rotatePosition = 0;
	if (minMatchPos.first >= minMatchPos.second)
	{
		rotatePosition = minMatchPos.first - minMatchPos.second;
	}
	else
	{
		assert(pathSeq.size() + minMatchPos.first >= minMatchPos.second);
		rotatePosition = pathSeq.size() + minMatchPos.first - minMatchPos.second;
	}
	size_t rotateNodes = 0;
	size_t pos = 0;
	size_t extraRotate = 0;
	for (size_t i = 1; i < result.nodes.size(); i++)
	{
		pos += graph.nodeSeqs.at(result.nodes[i-1].substr(1)).size();
		pos -= result.overlaps[i];
		if (pos > rotatePosition)
		{
			break;
		}
		rotateNodes = i;
		assert(rotatePosition >= pos);
		extraRotate = rotatePosition - pos;
	}
	assert(rotateNodes != std::numeric_limits<size_t>::max());
	assert(rotateNodes < result.nodes.size());
	std::cerr << "rotate by " << rotateNodes << " nodes and " << extraRotate << " base pairs" << std::endl;
	if (rotateNodes != 0)
	{
		Path result2;
		result2.nodes.insert(result2.nodes.end(), result.nodes.begin() + rotateNodes, result.nodes.end());
		result2.nodes.insert(result2.nodes.end(), result.nodes.begin(), result.nodes.begin() + rotateNodes);
		result2.overlaps.insert(result2.overlaps.end(), result.overlaps.begin() + rotateNodes, result.overlaps.end());
		result2.overlaps.insert(result2.overlaps.end(), result.overlaps.begin(), result.overlaps.begin() + rotateNodes);
		result = result2;
	}
	result.leftClip = extraRotate;
	if (result.overlaps[0] <= result.leftClip)
	{
		result.nodes.push_back(result.nodes[0]);
		result.overlaps.push_back(result.overlaps[0]);
		result.rightClip = graph.nodeSeqs.at(result.nodes.back().substr(1)).size() - extraRotate;
	}
	else
	{
		result.rightClip = result.overlaps[0] - result.leftClip;
	}
	return result;
}

void nameVariants(std::vector<Variant>& variants, const GfaGraph& graph, const Path& heavyPath)
{
	size_t pathLength = heavyPath.getSequence(graph.nodeSeqs).size();
	for (size_t variant = 0; variant < variants.size(); variant++)
	{
		std::string startNode = variants[variant].referencePath[0];
		std::string endNode = variants[variant].referencePath.back();
		size_t startIndex = std::numeric_limits<size_t>::max();
		size_t endIndex = std::numeric_limits<size_t>::max();
		size_t startPos = std::numeric_limits<size_t>::max();
		size_t endPos = std::numeric_limits<size_t>::max();
		size_t pathPos = 0;
		for (size_t i = 0; i < heavyPath.nodes.size(); i++)
		{
			if (i > 0)
			{
				pathPos += graph.nodeSeqs.at(heavyPath.nodes[i-1].substr(1)).size() - heavyPath.overlaps[i];
			}
			if (i == heavyPath.nodes.size()-1 && heavyPath.nodes[0] == heavyPath.nodes.back()) break;
			if (heavyPath.nodes[i] == startNode)
			{
				startIndex = i;
				startPos = pathPos;
			}
			if (heavyPath.nodes[i] == endNode)
			{
				endIndex = i;
				endPos = pathPos + graph.nodeSeqs.at(heavyPath.nodes[i].substr(1)).size();
			}
		}
		assert(startIndex != std::numeric_limits<size_t>::max());
		assert(endIndex != std::numeric_limits<size_t>::max());
		if (startPos >= heavyPath.leftClip)
		{
			variants[variant].referenceStartPos = startPos - heavyPath.leftClip;
		}
		else
		{
			variants[variant].referenceStartPos = pathLength + startPos - heavyPath.leftClip;
		}
		if (endPos >= heavyPath.leftClip)
		{
			variants[variant].referenceEndPos = endPos - heavyPath.leftClip;
		}
		else
		{
			variants[variant].referenceEndPos = pathLength + endPos - heavyPath.leftClip;
		}
		std::string referenceSeq = getSequence(variants[variant].referencePath, graph.nodeSeqs, graph.edges);
		std::string variantSeq = getSequence(variants[variant].path, graph.nodeSeqs, graph.edges);
		assert(variants[variant].referenceEndPos < variants[variant].referenceStartPos || referenceSeq.size() == variants[variant].referenceEndPos - variants[variant].referenceStartPos);
		assert(variants[variant].referenceEndPos > variants[variant].referenceStartPos || referenceSeq.size() == pathLength + variants[variant].referenceEndPos - variants[variant].referenceStartPos);
		size_t leftClip = 0;
		size_t rightClip = 0;
		if (graph.nodeSeqs.at(variants[variant].referencePath[0].substr(1)).size()+graph.nodeSeqs.at(variants[variant].referencePath.back().substr(1)).size()+2 >= referenceSeq.size())
		{
			while (leftClip < variantSeq.size() && leftClip < referenceSeq.size() && leftClip < graph.nodeSeqs.at(variants[variant].referencePath[0].substr(1)).size() && variantSeq[leftClip] == referenceSeq[leftClip]) leftClip += 1;
			while (rightClip < variantSeq.size() && rightClip < referenceSeq.size() && rightClip < graph.nodeSeqs.at(variants[variant].referencePath.back().substr(1)).size() && variantSeq[variantSeq.size()-1-rightClip] == referenceSeq[referenceSeq.size()-1-rightClip]) rightClip += 1;
			assert(leftClip == graph.nodeSeqs.at(variants[variant].referencePath[0].substr(1)).size());
			assert(rightClip == graph.nodeSeqs.at(variants[variant].referencePath.back().substr(1)).size());
		}
		while (leftClip + rightClip < variantSeq.size() && leftClip + rightClip < referenceSeq.size() && variantSeq[leftClip] == referenceSeq[leftClip]) leftClip += 1;
		while (leftClip + rightClip < variantSeq.size() && leftClip + rightClip < referenceSeq.size() && variantSeq[variantSeq.size()-1-rightClip] == referenceSeq[referenceSeq.size()-1-rightClip]) rightClip += 1;
		if (leftClip + rightClip == variantSeq.size() || leftClip + rightClip == referenceSeq.size())
		{
			assert(leftClip >= 1);
			leftClip -= 1;
		}
		variants[variant].variantSeq = variantSeq.substr(leftClip, variantSeq.size()-leftClip-rightClip);
		variants[variant].referenceSeq = referenceSeq.substr(leftClip, referenceSeq.size()-leftClip-rightClip);
		if (variants[variant].variantSeq.size() == 0) variants[variant].variantSeq = ".";
		if (variants[variant].referenceSeq.size() == 0) variants[variant].referenceSeq = ".";
		if (variants[variant].referenceStartPos + leftClip < pathLength)
		{
			variants[variant].referenceStartPos += leftClip;
		}
		else
		{
			variants[variant].referenceStartPos += leftClip;
			variants[variant].referenceStartPos -= pathLength;
		}
		if (variants[variant].referenceEndPos >= rightClip)
		{
			variants[variant].referenceEndPos -= rightClip;
		}
		else
		{
			variants[variant].referenceEndPos = pathLength + variants[variant].referenceEndPos - rightClip;
		}
	}
	std::sort(variants.begin(), variants.end(), [](const Variant& left, const Variant& right) { return left.referenceStartPos < right.referenceStartPos; });
	for (size_t variant = 0; variant < variants.size(); variant++)
	{
		variants[variant].name = "var" + std::to_string(variant) + "_" + std::to_string(variants[variant].referenceStartPos) + "_" + std::to_string(variants[variant].referenceEndPos);
	}
}

void writeVariantVCF(std::string filename, const Path& heavyPath, const GfaGraph& graph, const std::vector<Variant>& variants)
{
	size_t pathLength = heavyPath.getSequence(graph.nodeSeqs).size();
	std::ofstream file { filename };
	file << "##fileformat=VCFv4.2" << std::endl;
	file << "##contig=<ID=heavy_path,length=" << pathLength << ">" << std::endl;
	file << "##INFO=<ID=AD,Number=R,Type=Integer,Description=\"Total read depth for each allele\">" << std::endl;
	file << "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO" << std::endl;
	for (size_t i = 0; i < variants.size(); i++)
	{
		file << "heavy_path\t" << variants[i].referenceStartPos+1 << "\t" << variants[i].name << "\t" << variants[i].referenceSeq << "\t" << variants[i].variantSeq << "\t" << "." << "\tPASS\t" << "AD=" << variants[i].referenceCoverage << "," << variants[i].coverage << std::endl;
	}
}

void liftoverAnnotationsToConsensus(const std::string& basePath, const std::string& consensusPath, const std::string& annotationFasta, const std::string& annotationGff3)
{
	{
		std::ofstream typefile { basePath + "/liftoff_types.txt" };
		typefile << "rRNA" << std::endl;
		typefile << "misc_RNA" << std::endl;
		typefile << "repeat_region" << std::endl;
		typefile << "gene" << std::endl;
		typefile << "pseudogene" << std::endl;
	}
	std::string command = "liftoff -f " + basePath + "/liftoff_types.txt -g " + annotationGff3 + " -o " + basePath + "/annotation.gff3 -u " + basePath + "/unmapped_features.txt -dir " + basePath + "/liftoff_intermediate_files/ " + consensusPath + " " + annotationFasta;
	std::cerr << "running liftoff with command:" << std::endl;
	std::cerr << command << std::endl;
	system(command.c_str());
}

void alignONTToVariantGraph(std::string graphAlignerPath, std::string ontReadPath, std::string graphPath, std::string outputAlnPath, size_t numThreads)
{
	std::string graphalignerCommand;
	graphalignerCommand = graphAlignerPath + " -g " + graphPath + " -f " + ontReadPath + " -a " + outputAlnPath + " -t " + std::to_string(numThreads) + " --seeds-mxm-length 30 --seeds-mem-count 10000 --bandwidth 15 --multimap-score-fraction 0.99 --precise-clipping 0.85 --min-alignment-score 5000 --discard-cigar --clip-ambiguous-ends 100 --overlap-incompatible-cutoff 0.15 --mem-index-no-wavelet-tree --max-trace-count 5";
	std::cerr << "GraphAligner command:" << std::endl;
	std::cerr << graphalignerCommand << std::endl;
	system(graphalignerCommand.c_str());
}

std::string getPathSequence(const ReadPath& readPath, const GfaGraph& graph, const std::unordered_map<std::pair<std::string, std::string>, size_t>& edgeOverlaps)
{
	std::string result;
	for (size_t i = 0; i < readPath.path.size(); i++)
	{
		size_t overlap = 0;
		if (i > 0) overlap = edgeOverlaps.at(canon(readPath.path[i-1], readPath.path[i]));
		std::string add = graph.nodeSeqs.at(readPath.path[i].substr(1));
		if (readPath.path[i].substr(0) == "<")
		{
			add = revcomp(add);
		}
		if (overlap > 0) add = add.substr(overlap);
		result += add;
	}
	return result;
}

std::unordered_map<std::pair<std::string, std::string>, size_t> getEdgeOverlaps(const GfaGraph& graph)
{
	std::unordered_map<std::pair<std::string, std::string>, size_t> result;
	for (const auto& pair : graph.edges)
	{
		for (const auto& target : pair.second)
		{
			auto key = canon(pair.first, std::get<0>(target));
			size_t overlap = std::get<1>(target);
			assert(result.count(key) == 0 || result.at(key) == overlap);
			result[key] = overlap;
		}
	}
	return result;
}

std::unordered_map<std::string, bool> getNodeOrientations(const std::vector<std::string>& referencePath)
{
	std::unordered_map<std::string, bool> result;
	for (const auto& str : referencePath)
	{
		assert(result.count(str.substr(1)) == 0);
		result[str.substr(1)] = (str[0] == '>');
	}
	return result;
}

void orientPath(std::vector<std::string>& path, const std::unordered_map<std::string, bool>& referenceOrientations)
{
	size_t fwMatches = 0;
	size_t bwMatches = 0;
	for (const auto& node : path)
	{
		if (referenceOrientations.count(node.substr(1)) == 0) continue;
		bool orientHere = (node[0] == '>');
		bool refOrient = referenceOrientations.at(node.substr(1));
		if (orientHere == refOrient)
		{
			fwMatches += 1;
		}
		else
		{
			bwMatches += 1;
		}
	}
	if (bwMatches > fwMatches) path = reverse(path);
}

std::vector<std::string> extractCorrectedONTPaths(std::string gafFile, const Path& heavyPath, const size_t minLength, const GfaGraph& graph)
{
	std::vector<std::string> result;
	std::ifstream file { gafFile };
	auto edgeOverlaps = getEdgeOverlaps(graph);
	auto nodeOrientations = getNodeOrientations(heavyPath.nodes);
	while (file.good())
	{
		std::string line;
		getline(file, line);
		if (line.size() == 0) continue;
		std::stringstream sstr { line };
		ReadPath readPath;
		readPath.pathStartClip = 0;
		readPath.pathEndClip = 0;
		std::string pathstr;
		std::string dummy;
		size_t readlength;
		size_t mapq;
		sstr >> readPath.readName >> readlength >> readPath.readStart >> readPath.readEnd >> dummy >> pathstr >> dummy >> dummy >> dummy >> dummy >> dummy >> mapq;
		if (mapq < 20) continue;
		if (readPath.readEnd - readPath.readStart < minLength) continue;
		readPath.path = parsePath(pathstr);
		orientPath(readPath.path, nodeOrientations);
		result.emplace_back(getPathSequence(readPath, graph, edgeOverlaps));
	}
	return result;
}

std::vector<std::string> extractLoopSequences(const std::vector<std::string>& correctedReads, const Path& heavyPath, const size_t minLength, const GfaGraph& graph)
{
	const size_t k = 101;
	const size_t borderSize = 200;
	std::string consensusSequence = heavyPath.getSequence(graph.nodeSeqs);
	std::unordered_map<std::string, size_t> startKmers;
	std::unordered_map<std::string, size_t> endKmers;
	std::unordered_set<std::string> repeats;
	for (size_t i = 0; i < borderSize; i++)
	{
		std::string subs = consensusSequence.substr(i, k);
		if (endKmers.count(subs) == 1 || startKmers.count(subs) == 1)
		{
			endKmers[subs] = std::numeric_limits<size_t>::max();
			startKmers[subs] = std::numeric_limits<size_t>::max();
		}
		else
		{
			startKmers[subs] = i;
		}
		subs = consensusSequence.substr(consensusSequence.size()-k-i, k);
		if (endKmers.count(subs) == 1 || startKmers.count(subs) == 1)
		{
			endKmers[subs] = std::numeric_limits<size_t>::max();
			startKmers[subs] = std::numeric_limits<size_t>::max();
		}
		else
		{
			endKmers[subs] = i;
		}
	}
	for (size_t i = borderSize; i < consensusSequence.size() - borderSize; i++)
	{
		std::string subs = consensusSequence.substr(i, k);
		if (endKmers.count(subs) == 1 || startKmers.count(subs) == 1)
		{
			endKmers[subs] = std::numeric_limits<size_t>::max();
			startKmers[subs] = std::numeric_limits<size_t>::max();
		}
	}
	std::vector<std::string> result;
	for (const std::string& seq : correctedReads)
	{
		if (seq.size() < minLength) continue;
		std::vector<size_t> breakpoints;
		for (size_t i = 0; i < seq.size()-k; i++)
		{
			std::string subs = seq.substr(i, k);
			if (startKmers.count(subs) == 1)
			{
				size_t refPos = startKmers.at(subs);
				if (refPos != std::numeric_limits<size_t>::max())
				{
					if (i >= refPos) breakpoints.push_back(i - refPos);
				}
			}
			if (endKmers.count(subs) == 1)
			{
				size_t refPos = endKmers.at(subs);
				if (refPos != std::numeric_limits<size_t>::max())
				{
					if (i+refPos+k < seq.size()) breakpoints.push_back(i+refPos+k);
				}
			}
		}
		std::sort(breakpoints.begin(), breakpoints.end());
		std::vector<size_t> actualBreakpoints;
		for (size_t i = 10; i < breakpoints.size(); i++)
		{
			if (breakpoints[i-10] != breakpoints[i]) continue;
			if (actualBreakpoints.size() > 0 && breakpoints[i] == actualBreakpoints.back()) continue;
			if (actualBreakpoints.size() > 0 && breakpoints[i] < actualBreakpoints.back() + minLength) continue;
			actualBreakpoints.push_back(breakpoints[i]);
		}
		for (size_t i = 1; i < actualBreakpoints.size(); i++)
		{
			result.emplace_back(seq.substr(actualBreakpoints[i-1], actualBreakpoints[i] - actualBreakpoints[i-1]));
		}
	}
	return result;
}

size_t getEditDistance(const std::string& left, const std::string& right, const size_t maxEdits)
{
	if (left.size() > right.size()+maxEdits) return maxEdits+1;
	if (right.size() > left.size()+maxEdits) return maxEdits+1;
	assert(left.size() > maxEdits);
	assert(right.size() > maxEdits);
	std::vector<size_t> offsets;
	std::vector<size_t> nextOffsets;
	offsets.resize(1, 0);
	while (offsets[0] < left.size() && offsets[0] < right.size() && left[offsets[0]] == right[offsets[0]]) offsets[0] += 1;
	if (offsets[0] == left.size()) return right.size() - left.size();
	if (offsets[0] == right.size()) return left.size() - right.size();
	size_t zeroPos = 1;
	for (size_t score = 0; score < maxEdits; score++)
	{
		nextOffsets.resize(offsets.size()+2);
		for (size_t i = 0; i < nextOffsets.size(); i++)
		{
			nextOffsets[i] = 0;
			if (i >= 1 && i < nextOffsets.size()-1) nextOffsets[i] = offsets[i-1]+1;
			if (i >= 2) nextOffsets[i] = std::max(nextOffsets[i], offsets[i-2]);
			if (i < nextOffsets.size()-2) nextOffsets[i] = std::max(nextOffsets[i], offsets[i]+1);
			assert(nextOffsets[i] + i >= zeroPos);
			nextOffsets[i] = std::min(nextOffsets[i], right.size() - i + zeroPos);
			nextOffsets[i] = std::min(nextOffsets[i], left.size());
			while (nextOffsets[i] < left.size() && nextOffsets[i]+i-zeroPos < right.size() && left[nextOffsets[i]] == right[nextOffsets[i]+i-zeroPos]) nextOffsets[i] += 1;
			if (nextOffsets[i] == left.size() && nextOffsets[i]+i-zeroPos == right.size()) return score;
		}
		std::swap(offsets, nextOffsets);
		zeroPos += 1;
	}
	return maxEdits+1;
}

std::vector<std::vector<std::string>> clusterLoopSequences(const std::vector<std::string>& loops, const size_t maxEdits)
{
	std::vector<size_t> parent;
	parent.resize(loops.size());
	for (size_t i = 0; i < loops.size(); i++)
	{
		parent[i] = i;
	}
	for (size_t i = 0; i < loops.size(); i++)
	{
		while (parent[i] != parent[parent[i]]) parent[i] = parent[parent[i]];
		for (size_t j = 0; j < i; j++)
		{
			while (parent[j] != parent[parent[j]]) parent[j] = parent[parent[j]];
			if (parent[i] == parent[j]) continue;
			size_t edits = getEditDistance(loops[i], loops[j], maxEdits);
			if (edits > maxEdits) continue;
			parent[j] = parent[i];
		}
	}
	std::vector<size_t> parentToCluster;
	parentToCluster.resize(parent.size(), std::numeric_limits<size_t>::max());
	std::vector<std::vector<std::string>> result;
	for (size_t i = 0; i < parent.size(); i++)
	{
		while (parent[i] != parent[parent[i]]) parent[i] = parent[parent[i]];
		if (parent[i] != i) continue;
		parentToCluster[i] = result.size();
		result.emplace_back();
	}
	for (size_t i = 0; i < parent.size(); i++)
	{
		result[parentToCluster[parent[i]]].emplace_back(loops[i]);
	}
	return result;
}

std::string getConsensusSequence(const std::vector<std::string>& rawSequences)
{
	assert(rawSequences.size() >= 1);
	// todo implement
	return rawSequences[0];
}

std::vector<std::tuple<std::string, size_t>> getMorphConsensuses(const std::vector<std::vector<std::string>>& clusters)
{
	std::vector<std::tuple<std::string, size_t>> result;
	for (size_t i = 0; i < clusters.size(); i++)
	{
		result.emplace_back(getConsensusSequence(clusters[i]), clusters[i].size());
	}
	return result;
}

void writeMorphConsensuses(std::string outFile, const std::vector<std::tuple<std::string, size_t>>& morphConsensuses)
{
	std::ofstream file { outFile };
	for (size_t i = 0; i < morphConsensuses.size(); i++)
	{
		file << ">morphconsensus" << i << "_coverage" << std::get<1>(morphConsensuses[i]) << std::endl;
		file << std::get<0>(morphConsensuses[i]) << std::endl;
	}
}

void HandleCluster(const ClusterParams& params)
{
	std::cerr << "running MBG" << std::endl;
	runMBG(params.basePath, params.hifiReadPath, params.MBGPath, params.k);
	std::cerr << "reading graph" << std::endl;
	GfaGraph graph;
	graph.loadFromFile(params.basePath + "/graph.gfa");
	std::cerr << "getting consensus" << std::endl;
	Path heavyPath = getHeavyPath(graph);
	if (params.orientReferencePath.size() > 0)
	{
		std::cerr << "orienting consensus" << std::endl;
		heavyPath = orientPath(graph, heavyPath, params.orientReferencePath, 101);
	}
	std::cerr << "writing consensus" << std::endl;
	writePathSequence(heavyPath, graph, params.basePath + "/consensus.fa");
	writePathGaf(heavyPath, graph, params.basePath + "/consensus_path.gaf");
	std::cerr << "reading read paths" << std::endl;
	std::vector<ReadPath> readPaths = loadReadPaths(params.basePath + "/paths.gaf");
	std::cerr << "getting variants" << std::endl;
	std::vector<Variant> variants = getVariants(graph, heavyPath, readPaths, 3);
	nameVariants(variants, graph, heavyPath);
	std::cerr << "writing variants" << std::endl;
	writeVariants(heavyPath, graph, variants, params.basePath + "/variants.txt");
	std::cerr << "making variant graph" << std::endl;
	writeVariantGraph(params.basePath + "/graph.gfa", heavyPath, variants, params.basePath + "/variant-graph.gfa");
	std::cerr << "writing variant vcf" << std::endl;
	writeVariantVCF(params.basePath + "/variants.vcf", heavyPath, graph, variants);
	if (params.annotationFasta.size() > 0)
	{
		std::cerr << "lifting over annotations to consensus" << std::endl;
		liftoverAnnotationsToConsensus(params.basePath, params.basePath + "/consensus.fa", params.annotationFasta, params.annotationGff3);
	}
	if (params.ontReadPath.size() > 0)
	{
		std::cerr << "aligning ultralong ONT reads to variant graph" << std::endl;
		alignONTToVariantGraph(params.GraphAlignerPath, params.ontReadPath, params.basePath + "/variant-graph.gfa", params.basePath + "/ont-alns.gaf", params.numThreads);
		std::cerr << "extract corrected ultralong paths" << std::endl;
		size_t heavyPathLength = heavyPath.getSequence(graph.nodeSeqs).size();
		size_t minLength = heavyPathLength * 0.5;
		std::cerr << "consensus path length " << heavyPathLength << ", using " << minLength << " as minimum morph length" << std::endl;
		auto ontPaths = extractCorrectedONTPaths(params.basePath + "/ont-alns.gaf", heavyPath, minLength, graph);
		std::cerr << ontPaths.size() << " corrected paths" << std::endl;
		std::cerr << "extract corrected morph sequences from ONTs" << std::endl;
		auto loopSequences = extractLoopSequences(ontPaths, heavyPath, minLength, graph);
		std::cerr << loopSequences.size() << " morph sequences in ONTs" << std::endl;
		{
			std::ofstream file { params.basePath + "/loops.fa" };
			for (size_t i = 0; i < loopSequences.size(); i++)
			{
				file << ">loop" << i << std::endl;
				file << loopSequences[i] << std::endl;
			}
		}
		std::cerr << "cluster morphs" << std::endl;
		auto clusters = clusterLoopSequences(loopSequences, 300);
		std::cerr << clusters.size() << " morph clusters" << std::endl;
		std::cerr << "getting morph consensuses" << std::endl;
		auto morphConsensuses = getMorphConsensuses(clusters);
		std::cerr << "write morph consensuses" << std::endl;
		writeMorphConsensuses(params.basePath + "/morphs.fa", morphConsensuses);
	}
}
