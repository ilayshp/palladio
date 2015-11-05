#include "client/SOP_prt4houdini.h"
#include "client/utils.h"

#include "codec/encoder/HoudiniCallbacks.h"

#include "prt/API.h"
#include "prt/FlexLicParams.h"

#ifndef WIN32
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#	pragma GCC diagnostic ignored "-Wattributes"
#endif
#include "GU/GU_Detail.h"
#include "GU/GU_PrimPoly.h"
#include "PRM/PRM_Include.h"
#include "PRM/PRM_SpareData.h"
#include "PRM/PRM_ChoiceList.h"
#include "OP/OP_Operator.h"
#include "OP/OP_OperatorTable.h"
#include "OP/OP_Director.h"
#include "SOP/SOP_Guide.h"
#include "GOP/GOP_GroupParse.h"
#include "GEO/GEO_PolyCounts.h"
#include "PI/PI_EditScriptedParms.h"
#include "UT/UT_DSOVersion.h"
#include "UT/UT_Matrix3.h"
#include "UT/UT_Matrix4.h"
#include "UT/UT_Exit.h"
#include "UT/UT_Interrupt.h"
#include "SYS/SYS_Math.h"
#include <HOM/HOM_Module.h>
#include <HOM/HOM_SopNode.h>

#include "boost/foreach.hpp"
#include "boost/filesystem.hpp"
#include "boost/dynamic_bitset.hpp"
#include "boost/algorithm/string.hpp"

#ifdef WIN32
#	include "boost/assign.hpp"
#endif

#ifndef WIN32
#	pragma GCC diagnostic pop
#endif

#include <vector>
#include <cwchar>


namespace {

const bool DBG = false;

// global prt settings
const prt::LogLevel	PRT_LOG_LEVEL		= prt::LOG_DEBUG;
const char*			PRT_LIB_SUBDIR		= "prtlib";
const char*			FILE_FLEXNET_LIB	= "flexnet_prt";
const wchar_t*		FILE_CGA_ERROR		= L"CGAErrors.txt";
const wchar_t*		FILE_CGA_PRINT		= L"CGAPrint.txt";

// some encoder IDs
const wchar_t*	ENCODER_ID_CGA_EVALATTR	= L"com.esri.prt.core.AttributeEvalEncoder";
const wchar_t*	ENCODER_ID_CGA_ERROR	= L"com.esri.prt.core.CGAErrorEncoder";
const wchar_t*	ENCODER_ID_CGA_PRINT	= L"com.esri.prt.core.CGAPrintEncoder";
const wchar_t*	ENCODER_ID_HOUDINI		= L"HoudiniEncoder";

// objects with same lifetime as PRT process
const prt::Object* prtLicHandle = 0;

void dsoExit(void*) {
	if (prtLicHandle)
		prtLicHandle->destroy(); // prt shutdown
}

class HoudiniGeometry : public HoudiniCallbacks {
public:
	HoudiniGeometry(GU_Detail* gdp, prt::AttributeMapBuilder* eab = nullptr)
	: mDetail(gdp), mEvalAttrBuilder(eab), mCurOffset(0) { }

protected:
	virtual void setVertices(double* vtx, size_t size) {
		mPoints.reserve(size / 3);
		for (size_t pi = 0; pi < size; pi += 3)
			mPoints.push_back(UT_Vector3(vtx[pi], vtx[pi+1], vtx[pi+2]));
	}

	virtual void setNormals(double* nrm, size_t size) {
		// TODO
	}

	virtual void setUVs(float* u, float* v, size_t size) {
//		GA_RWHandleV3 txth = GA_RWHandleV3(mDetails->addTextureAttribute(GA_ATTRIB_PRIMITIVE));
	// TODO
	}

	virtual void setFaces(int* counts, size_t countsSize, int* connects, size_t connectsSize, int* uvCounts, size_t uvCountsSize, int* uvConnects, size_t uvConnectsSize) {
		for (size_t ci = 0; ci < countsSize; ci++)
			mPolyCounts.append(counts[ci]);
		mIndices.reserve(connectsSize);
		mIndices.insert(mIndices.end(), connects, connects+connectsSize);
	}

	virtual void createMesh(const wchar_t* name) {
		std::string nname = prt4hdn::utils::toOSNarrowFromUTF16(name);
		mCurGroup = static_cast<GA_PrimitiveGroup*>(mDetail->getElementGroupTable(GA_ATTRIB_PRIMITIVE).newGroup(nname.c_str(), false));
		if (DBG) LOG_DBG << "createMesh: " << nname;
	}

	virtual void matSetColor(int start, int count, float r, float g, float b) {
		LOG_DBG << "matSetColor: start = " << start << ", count = " << count << ", rgb = " << r << ", " << g << ", " << b;
		GA_Offset off = mCurOffset + start;
		GA_RWHandleV3 c(mDetail->addDiffuseAttribute(GA_ATTRIB_PRIMITIVE));
		UT_Vector3 color(r, g, b);
		for (int i = 0; i < count; i++) {
			c.set(off++, color);
		}
	}

	virtual void matSetDiffuseTexture(int start, int count, const wchar_t* tex) {
//		LOG_DBG << L"matSetDiffuseTexture: " << start << L", count = " << count << L", tex = " << tex;
//		GA_RWHandleV3 txth = GA_RWHandleV3(mDetails->addTextureAttribute(GA_ATTRIB_PRIMITIVE));
//		GA_Offset off = mCurOffset + start;
//		for (int i = 0; i < count; i++)
//			txth.set(off++, )
	}

	virtual void finishMesh() {
		GA_IndexMap::Marker marker(mDetail->getPrimitiveMap());
		mCurOffset = GU_PrimPoly::buildBlock(mDetail, &mPoints[0], mPoints.size(), mPolyCounts, &mIndices[0]);
		mCurGroup->addRange(marker.getRange());
		mPolyCounts.clear();
		mIndices.clear();
		mPoints.clear();
		if (DBG) LOG_DBG << "finishMesh";
	}

	virtual prt::Status generateError(size_t isIndex, prt::Status status, const wchar_t* message) {
		LOG_ERR << message;
		return prt::STATUS_OK;
	}
	virtual prt::Status assetError(size_t isIndex, prt::CGAErrorLevel level, const wchar_t* key, const wchar_t* uri, const wchar_t* message) {
		LOG_WRN << key << L": " << message;
		return prt::STATUS_OK;
	}
	virtual prt::Status cgaError(size_t isIndex, int32_t shapeID, prt::CGAErrorLevel level, int32_t methodId, int32_t pc, const wchar_t* message) {
		LOG_ERR << message;
		return prt::STATUS_OK;
	}
	virtual prt::Status cgaPrint(size_t isIndex, int32_t shapeID, const wchar_t* txt) {
		return prt::STATUS_OK;
	}
	virtual prt::Status cgaReportBool(size_t isIndex, int32_t shapeID, const wchar_t* key, bool value) {
		return prt::STATUS_OK;
	}
	virtual prt::Status cgaReportFloat(size_t isIndex, int32_t shapeID, const wchar_t* key, double value) {
		return prt::STATUS_OK;
	}
	virtual prt::Status cgaReportString(size_t isIndex, int32_t shapeID, const wchar_t* key, const wchar_t* value) {\
		return prt::STATUS_OK;
	}
	virtual prt::Status attrBool(size_t isIndex, int32_t shapeID, const wchar_t* key, bool value) {
		if (mEvalAttrBuilder != nullptr) {
			mEvalAttrBuilder->setBool(key, value);
		}
		return prt::STATUS_OK;
	}
	virtual prt::Status attrFloat(size_t isIndex, int32_t shapeID, const wchar_t* key, double value) {
		if (mEvalAttrBuilder != nullptr) {
			mEvalAttrBuilder->setFloat(key, value);
		}
		return prt::STATUS_OK;
	}
	virtual prt::Status attrString(size_t isIndex, int32_t shapeID, const wchar_t* key, const wchar_t* value) {
		if (mEvalAttrBuilder != nullptr) {
			mEvalAttrBuilder->setString(key, value);
		}
		return prt::STATUS_OK;
	}

private:
	GU_Detail* mDetail;
	GA_Offset mCurOffset;
	GA_PrimitiveGroup* mCurGroup;
	std::vector<UT_Vector3> mPoints;
	std::vector<int> mIndices;
	GEO_PolyCounts mPolyCounts;

	prt::AttributeMapBuilder* const mEvalAttrBuilder;
};

typedef std::vector<const prt::InitialShape*> InitialShapePtrVector;
typedef std::vector<const prt::AttributeMap*> AttributeMapPtrVector;

struct InitialShapeContext {
	InitialShapeContext(OP_Context& ctx, GU_Detail& detail) : mContext(ctx) {
		LOG_DBG << "-- creating intial shape context";

		//mAMB = prt::AttributeMapBuilder::create();
		mISB = prt::InitialShapeBuilder::create();

		// collect all primitive attributes
		// TODO: GA_AttributeDict is just for bw comp, use GA_AttributeSet
		for (GA_AttributeDict::iterator it = detail.getAttributeDict(GA_ATTRIB_PRIMITIVE).begin(GA_SCOPE_PUBLIC); !it.atEnd(); ++it) {
			mPrimitiveAttributes.push_back(GA_ROAttributeRef(it.attrib()));
			LOG_DBG << "    prim attr: " << mPrimitiveAttributes.back()->getName();
		}
		LOG_DBG << "    got primitive attribute count = " << mPrimitiveAttributes.size();

	}

	~InitialShapeContext() {
		BOOST_FOREACH(const prt::InitialShape* is, mInitialShapes) {
			is->destroy();
		}
		mISB->destroy();
		BOOST_FOREACH(const prt::AttributeMap* am, mInitialShapeAttributes) {
			am->destroy();
		}
		//mAMB->destroy();
	}

	OP_Context& mContext;
	std::vector<GA_ROAttributeRef> mPrimitiveAttributes;

	prt::InitialShapeBuilder* mISB;
	InitialShapePtrVector mInitialShapes;
	//prt::AttributeMapBuilder* mAMB;
	AttributeMapPtrVector mInitialShapeAttributes;
};

const char* NODE_PARAM_RPK			= "rpk";
const char* NODE_PARAM_RULE_FILE	= "ruleFile";
const char* NODE_PARAM_STYLE		= "style";
const char* NODE_PARAM_START_RULE	= "startRule";
const char* NODE_PARAM_SEED			= "seed";
const char* NODE_PARAM_LOG			= "logLevel";

PRM_Name NODE_PARAM_NAMES[] = {
		PRM_Name(NODE_PARAM_RPK,		"Rule Package"),
		PRM_Name(NODE_PARAM_RULE_FILE,	"Rule File"),
		PRM_Name(NODE_PARAM_STYLE,		"Style"),
		PRM_Name(NODE_PARAM_START_RULE,	"Start Rule"),
		PRM_Name(NODE_PARAM_SEED,		"Rnd Seed"),
		PRM_Name(NODE_PARAM_LOG,		"Log Level")
};

PRM_Default rpkDefault(0, "$HIP/$F.rpk");
PRM_Default startRuleDefault(0, "Start");
PRM_Default logDefault(0, "DEBUG");

PRM_ChoiceList startRuleMenu((PRM_ChoiceListType)(PRM_CHOICELIST_EXCLUSIVE | PRM_CHOICELIST_REPLACE), &prt4hdn::SOP_PRT::buildStartRuleMenu);

PRM_Name logNames[] = {
	PRM_Name("TRACE", "trace"), // TODO: eventually, remove this and offset index by 1
	PRM_Name("DEBUG", "debug"),
	PRM_Name("INFO", "info"),
	PRM_Name("WARNING", "warning"),
	PRM_Name("ERROR", "error"),
	PRM_Name("FATAL", "fatal"),
	PRM_Name(0)
};
PRM_ChoiceList logMenu((PRM_ChoiceListType)(PRM_CHOICELIST_EXCLUSIVE | PRM_CHOICELIST_REPLACE), logNames);

PRM_Template NODE_PARAM_TEMPLATES[] = {
		PRM_Template(PRM_FILE,		1, &NODE_PARAM_NAMES[0],	&rpkDefault, 0, 0, 0, &PRM_SpareData::fileChooserModeRead),
		PRM_Template(PRM_STRING,	1, &NODE_PARAM_NAMES[1],	PRMoneDefaults),
		PRM_Template(PRM_STRING,	1, &NODE_PARAM_NAMES[2],	PRMoneDefaults),
		PRM_Template(PRM_STRING,	1, &NODE_PARAM_NAMES[3],	&startRuleDefault,	&startRuleMenu),
		PRM_Template(PRM_INT,		1, &NODE_PARAM_NAMES[4],	PRMoneDefaults),
		PRM_Template((PRM_Type)PRM_ORD, PRM_Template::PRM_EXPORT_MAX, 1, &NODE_PARAM_NAMES[5], 0, &logMenu),
		PRM_Template()
};

} // namespace anonymous


// TODO: add support for multiple nodes
void newSopOperator(OP_OperatorTable *table) {
	UT_Exit::addExitCallback(dsoExit);

	boost::filesystem::path sopPath;
	prt4hdn::utils::getPathToCurrentModule(sopPath);

	prt::FlexLicParams flp;
	std::string libflexnet = prt4hdn::utils::getSharedLibraryPrefix() + FILE_FLEXNET_LIB + prt4hdn::utils::getSharedLibrarySuffix();
	std::string libflexnetPath = (sopPath.parent_path() / libflexnet).string();
	flp.mActLibPath = libflexnetPath.c_str();
	flp.mFeature = "CityEngAdvFx";
	flp.mHostName = "";

	std::wstring libPath = (sopPath.parent_path() / PRT_LIB_SUBDIR).wstring();
	const wchar_t* extPaths[] = { libPath.c_str() };

	prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
	prtLicHandle = prt::init(extPaths, 1, PRT_LOG_LEVEL, &flp, &status); // TODO: add UI for log level control

	if (prtLicHandle == 0 || status != prt::STATUS_OK)
		return;

	const size_t minSources = 1;
	const size_t maxSources = 1;
	table->addOperator(new OP_Operator("prt4houdini", "prt4houdini", prt4hdn::SOP_PRT::create,
			NODE_PARAM_TEMPLATES, minSources, maxSources, 0)
	);
}


namespace prt4hdn {

OP_Node* SOP_PRT::create(OP_Network *net, const char *name, OP_Operator *op) {
	return new SOP_PRT(net, name, op);
}

SOP_PRT::SOP_PRT(OP_Network *net, const char *name, OP_Operator *op)
: SOP_Node(net, name, op)
, mLogHandler(new log::LogHandler(utils::toUTF16FromOSNarrow(name), prt::LOG_DEBUG))
, mPRTCache(prt::CacheObject::create(prt::CacheObject::CACHE_TYPE_DEFAULT))
, mAssetsMap(nullptr)
, mAttributeSource(prt::AttributeMapBuilder::create())
{
	prt::addLogHandler(mLogHandler.get());

	prt::AttributeMapBuilder* optionsBuilder = prt::AttributeMapBuilder::create();

	const prt::AttributeMap* encoderOptions = optionsBuilder->createAttributeMapAndReset();
	mHoudiniEncoderOptions = utils::createValidatedOptions(ENCODER_ID_HOUDINI, encoderOptions);
	encoderOptions->destroy();

	optionsBuilder->setString(L"name", FILE_CGA_ERROR);
	const prt::AttributeMap* errOptions = optionsBuilder->createAttributeMapAndReset();
	mCGAErrorOptions = utils::createValidatedOptions(ENCODER_ID_CGA_ERROR, errOptions);
	errOptions->destroy();

	optionsBuilder->setString(L"name", FILE_CGA_PRINT);
	const prt::AttributeMap* printOptions = optionsBuilder->createAttributeMapAndReset();
	mCGAPrintOptions = utils::createValidatedOptions(ENCODER_ID_CGA_PRINT, printOptions);
	printOptions->destroy();

	optionsBuilder->destroy();

#ifdef WIN32
	mAllEncoders = boost::assign::list_of(ENCODER_ID_HOUDINI)(ENCODER_ID_CGA_ERROR)(ENCODER_ID_CGA_PRINT);
	mAllEncoderOptions = boost::assign::list_of(mHoudiniEncoderOptions)(mCGAErrorOptions)(mCGAPrintOptions);
#else
	mAllEncoders = { ENCODER_ID_HOUDINI, ENCODER_ID_CGA_ERROR, ENCODER_ID_CGA_PRINT };
	mAllEncoderOptions = { mHoudiniEncoderOptions, mCGAErrorOptions, mCGAPrintOptions };
#endif
}

SOP_PRT::~SOP_PRT() {
	mAttributeSource->destroy();

	if (mAssetsMap != nullptr)
		mAssetsMap->destroy();

	mHoudiniEncoderOptions->destroy();
	mCGAErrorOptions->destroy();
	mCGAPrintOptions->destroy();

	mPRTCache->destroy();

	prt::removeLogHandler(mLogHandler.get());
}

void SOP_PRT::createInitialShape(const GA_Group* group, void* ctx) {
	static const bool DBG = false;

	InitialShapeContext* isc = static_cast<InitialShapeContext*>(ctx);
	if (DBG) {
		LOG_DBG << "-- creating initial shape geo from group " << group->getName(); // << ": vtx = " << vtx << ", idx = " << idx << ", faceCounts = " << faceCounts;
	}

	fpreal t = isc->mContext.getTime();

	// convert geometry
	std::vector<double> vtx;
	std::vector<uint32_t> idx, faceCounts;

	GA_Offset ptoff;
	GA_FOR_ALL_PTOFF(gdp, ptoff) {
		UT_Vector3 p = gdp->getPos3(ptoff);
		vtx.push_back(static_cast<double>(p.x()));
		vtx.push_back(static_cast<double>(p.y()));
		vtx.push_back(static_cast<double>(p.z()));
	}

	// loop over all polygons in the primitive group
	// in case of multipoly initial shapes, attr on first one wins
	boost::dynamic_bitset<> setAttributes(isc->mPrimitiveAttributes.size());
	GA_Primitive* prim = nullptr;
	GA_FOR_ALL_GROUP_PRIMITIVES(gdp, static_cast<const GA_PrimitiveGroup*>(group), prim) {
		GU_PrimPoly* face = static_cast<GU_PrimPoly*>(prim);
		for(GA_Size i = face->getVertexCount()-1; i >= 0 ; i--) {
			GA_Offset off = face->getPointOffset(i);
			idx.push_back(static_cast<uint32_t>(off));
		}
		faceCounts.push_back(static_cast<uint32_t>(face->getVertexCount()));

		// extract primitive attributes
		for (size_t ai = 0; ai < isc->mPrimitiveAttributes.size(); ai++) {
			if (!setAttributes[ai]) {
				const GA_ROAttributeRef& ar = isc->mPrimitiveAttributes[ai];
				if (ar.isFloat() || ar.isInt()) {
					double v = prim->getValue<double>(ar);
					if (DBG) LOG_DBG << "   setting float attrib " << ar->getName() << " = " << v;
					std::wstring wn = utils::toUTF16FromOSNarrow(ar->getName());
					mAttributeSource->setFloat(wn.c_str(), v);
					setAttributes.set(ai);
				} else if (ar.isString()) {
					const char* v = prim->getString(ar);
					if (DBG) LOG_DBG << "   setting string attrib " << ar->getName() << " = " << v;
					std::wstring wn = utils::toUTF16FromOSNarrow(ar->getName());
					std::wstring wv = utils::toUTF16FromOSNarrow(v);
					mAttributeSource->setString(wn.c_str(), wv.c_str());
					setAttributes.set(ai);
				}
			}
		}
	}

	const prt::AttributeMap* initialShapeAttrs = mAttributeSource->createAttributeMap();

	isc->mISB->setGeometry(vtx.data(), vtx.size(), idx.data(), idx.size(), faceCounts.data(), faceCounts.size());

	std::wstring shapeName = utils::toUTF16FromOSNarrow(group->getName().toStdString());
	std::wstring startRule = mStyle + L"$" + mStartRule;
	isc->mISB->setAttributes(
			mRuleFile.c_str(),
			startRule.c_str(),
			mSeed,
			shapeName.c_str(),
			initialShapeAttrs,
			mAssetsMap
	);

	prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
	const prt::InitialShape* initialShape = isc->mISB->createInitialShapeAndReset(&status);
	if (status != prt::STATUS_OK) {
		LOG_WRN << "ignored input group '" << group->getName() << "': " << prt::getStatusDescription(status);
		initialShapeAttrs->destroy();
		return;
	}

	if (DBG) LOG_DBG << prt4hdn::utils::objectToXML(initialShape);

	isc->mInitialShapes.push_back(initialShape);
	isc->mInitialShapeAttributes.push_back(initialShapeAttrs);
}

OP_ERROR SOP_PRT::cookMySop(OP_Context &context) {
	if (!handleParams(context))
		return error();

	if (lockInputs(context) >= UT_ERROR_ABORT) {
		LOG_DBG << "lockInputs error";
		return error();
	}

	duplicateSource(0, context);

	if (error() < UT_ERROR_ABORT && cookInputGroups(context) < UT_ERROR_ABORT) {
		UT_AutoInterrupt progress("Generating PRT Geometry...");

		InitialShapeContext isc(context, *gdp);
		const char* pattern = "*";
		GroupOperation createInitialShapeOp = static_cast<GroupOperation>(&SOP_PRT::createInitialShape);
		forEachGroupMatchingMask(pattern, createInitialShapeOp, static_cast<void*>(&isc), GA_GROUP_PRIMITIVE, gdp);

		gdp->clearAndDestroy();

		HoudiniGeometry hg(gdp);
		{
			prt::Status stat = prt::generate(
					&isc.mInitialShapes[0], isc.mInitialShapes.size(), 0,
					&mAllEncoders[0], mAllEncoders.size(), &mAllEncoderOptions[0],
					&hg, mPRTCache, 0
			);
			if(stat != prt::STATUS_OK) {
				LOG_ERR << "prt::generate() failed with status: '" << prt::getStatusDescription(stat) << "' (" << stat << ")";
			}
		}

		select(GU_SPrimitive);
	}

	unlockInputs();
	return error();
}

namespace {

void getParamDef(
		const prt::RuleFileInfo* info,
		SOP_PRT::TypedParamNames& createdParams,
		std::ostream& defStream
) {
	for(size_t i = 0; i < info->getNumAttributes(); i++) {
		if (info->getAttribute(i)->getNumParameters() != 0)
			continue;

		const wchar_t* attrName = info->getAttribute(i)->getName();
		std::string nAttrName = utils::toOSNarrowFromUTF16(attrName);
		nAttrName = nAttrName.substr(8); // TODO: better way to remove style for now...

		defStream << "parm {" << "\n";
		defStream << "    name  \"" << nAttrName << "\"" << "\n";
		defStream << "    label \"" << nAttrName << "\"\n";

		switch(info->getAttribute(i)->getReturnType()) {
		case prt::AAT_BOOL: {
			defStream << "    type    integer\n";
			break;
		}
		case prt::AAT_FLOAT: {
			defStream << "    type    float\n";
			// TODO: handle @RANGE annotation:	parmDef << "range   { 0 10 }\n";
			createdParams[prt::Attributable::PT_FLOAT].push_back(nAttrName);
			break;
		}
		case prt::AAT_STR: {
			defStream << "    type    string\n";
			createdParams[prt::Attributable::PT_STRING].push_back(nAttrName);
			break;
		}
		default:
			break;
		}

		defStream << "    size    1\n";
		defStream << "    export  none\n";
		defStream << "}\n";
	}
}

namespace UnitQuad {
const double 	vertices[]				= { 0, 0, 0,  0, 0, 1,  1, 0, 1,  1, 0, 0 };
const size_t 	vertexCount				= 12;
const uint32_t	indices[]				= { 0, 1, 2, 3 };
const size_t 	indexCount				= 4;
const uint32_t 	faceCounts[]			= { 4 };
const size_t 	faceCountsCount			= 1;
}

}

bool SOP_PRT::handleParams(OP_Context &context) {
	LOG_DBG << "handleParams()";
	fpreal now = context.getTime();

	// -- rule file
	// TODO: search/list cgbs
	// mRuleFile = L"bin/Candler Building.cgb";
	UT_String utRuleFile;
	evalString(utRuleFile, NODE_PARAM_RULE_FILE, 0, now);
	mRuleFile = utils::toUTF16FromOSNarrow(utRuleFile.toStdString());
	LOG_DBG << L"got rule file: " << mRuleFile;
	if (mRuleFile.empty()) {
		LOG_ERR << "rule file is empty/invalid, cannot continue";
		return false;
	}

	// -- style
	// TODO: list styles dynamically
	UT_String utStyle;
	evalString(utStyle, NODE_PARAM_STYLE, 0, now);
	mStyle = utils::toUTF16FromOSNarrow(utStyle.toStdString());

	// -- start rule
	UT_String utStartRule;
	evalString(utStartRule, NODE_PARAM_START_RULE, 0, now);
	mStartRule = utils::toUTF16FromOSNarrow(utStartRule.toStdString());

//	LOG_DBG << L"'style = " << mStyle << L", start rule = " << mStartRule;

	// -- random seed
	mSeed = evalInt(NODE_PARAM_SEED, 0, now);

	// -- logging level
	prt::LogLevel ll = static_cast<prt::LogLevel>(evalInt(NODE_PARAM_LOG, 0, now));
	mLogHandler->setLevel(ll);
	mLogHandler->setName(utils::toUTF16FromOSNarrow(getName().toStdString()));

	// -- rule package
	UT_String utNextRPKStr;
	evalString(utNextRPKStr, NODE_PARAM_RPK, 0, now);
	boost::filesystem::path nextRPKPath(utNextRPKStr.toStdString());
	if (boost::filesystem::exists(nextRPKPath)) {
		std::wstring nextRPKURI = L"file://" + nextRPKPath.wstring();
		if (nextRPKURI != mRPKURI) {
			LOG_DBG << L"detected new RPK path: " << nextRPKURI;

			// clean up previous rpk context
			if (mAssetsMap != nullptr) {
				mAssetsMap->destroy();
				mAssetsMap = nullptr;
			}
			mPRTCache->flushAll();

			boost::filesystem::path unpackPath = boost::filesystem::temp_directory_path();

			// rebuild assets map
			prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
			mAssetsMap = prt::createResolveMap(nextRPKURI.c_str(), unpackPath.wstring().c_str(), &status);
			if(status != prt::STATUS_OK) {
				LOG_ERR << "failed to create resolve map from '" << nextRPKURI << "', aborting.";
				return false;
			}

			// rebuild attribute UI
			status = prt::STATUS_UNSPECIFIED_ERROR;
//			LOG_DBG << L"going to get cgbURI: mAssetsMap = " << mAssetsMap << L", mRuleFile = " << mRuleFile;
			if (!mAssetsMap->hasKey(mRuleFile.c_str()))
				return false;

			const wchar_t* cgbURI = mAssetsMap->getString(mRuleFile.c_str());
			LOG_DBG << L"got cgbURI = " << cgbURI;
			if (cgbURI == nullptr) {
				LOG_ERR << L"failed to resolve rule file '" << mRuleFile << "', aborting.";
				return false;
			}
//			LOG_DBG << "going to createRuleFileInfo";
			const prt::RuleFileInfo* info = prt::createRuleFileInfo(cgbURI, 0, &status);
			if (status == prt::STATUS_OK) {

				// eval rule attribute values
				prt::AttributeMapBuilder* amb = prt::AttributeMapBuilder::create();
				{
					HoudiniGeometry hg(nullptr, amb);
					const prt::AttributeMap* emptyAttrMap = amb->createAttributeMapAndReset();

					prt::InitialShapeBuilder* isb = prt::InitialShapeBuilder::create();
					isb->setGeometry(UnitQuad::vertices, UnitQuad::vertexCount, UnitQuad::indices, UnitQuad::indexCount, UnitQuad::faceCounts, UnitQuad::faceCountsCount);
					std::wstring startRule = mStyle + L"$" + mStartRule;
					isb->setAttributes(mRuleFile.c_str(), startRule.c_str(), 666, L"temp", emptyAttrMap, mAssetsMap);

					prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
					const prt::InitialShape* is = isb->createInitialShapeAndReset(&status);
					isb->destroy();
					const prt::InitialShape* iss[1] = { is };

					const prt::EncoderInfo* encInfo = prt::createEncoderInfo(ENCODER_ID_CGA_EVALATTR);
					const prt::AttributeMap* encOpts = nullptr;
					encInfo->createValidatedOptionsAndStates(nullptr, &encOpts);
					encInfo->destroy();

					const wchar_t* encs[1] = { ENCODER_ID_CGA_EVALATTR };
					const prt::AttributeMap* encsOpts[1] = { encOpts };

					prt::Status stat = prt::generate(iss, 1, nullptr, encs, 1, encsOpts, &hg, mPRTCache, nullptr, nullptr);
					if(stat != prt::STATUS_OK) {
						LOG_ERR << "prt::generate() failed with status: '" << prt::getStatusDescription(stat) << "' (" << stat << ")";
					}

					encOpts->destroy();
					is->destroy();
					emptyAttrMap->destroy();
				}

				const prt::AttributeMap* defAttrVals = amb->createAttributeMap();
				amb->destroy();
				LOG_DBG << "defAttrVals = " << utils::objectToXML(defAttrVals);

				// build spare param definition
				mActiveParams.clear();
				std::ostringstream paramDef;
				getParamDef(info, mActiveParams, paramDef);
				std::string s = paramDef.str();
				LOG_DBG << " INPUT PARMS" << s;

				OP_Director* director = OPgetDirector();
				director->deleteAllNodeSpareParms(this);

				UT_IStream istr(&s[0], s.size(), UT_ISTREAM_ASCII);
				UT_String errors;
				director->loadNodeSpareParms(this, istr, errors);

				// setup spare params with rule defaults
				size_t keyCount = 0;
				const wchar_t* const* cKeys = defAttrVals->getKeys(&keyCount);
				for (size_t k = 0; k < keyCount; k++) {
					const wchar_t* key = cKeys[k];
					std::string nKey = utils::toOSNarrowFromUTF16(key);
					nKey = nKey.substr(8); // TODO: better way to remove style for now...
					switch (defAttrVals->getType(key)) {
					case prt::AttributeMap::PT_FLOAT: {
						setFloat(nKey.c_str(), 0, 0.0, defAttrVals->getFloat(key));
						break;
					}
					case prt::AttributeMap::PT_STRING: {
						UT_String val(utils::toOSNarrowFromUTF16(defAttrVals->getString(key)));
						setString(val, CH_STRING_LITERAL, nKey.c_str(), 0, 0.0);
						break;
					}
					default: {
						LOG_WRN << "attribute " << nKey << ": type not handled";
						break;
					}
					}
				}

#if 0
				std::ostringstream ostr;
				director->saveNodeSpareParms(this, true, ostr);
				LOG_DBG << " SAVED PARMS" << ostr.str();
#endif

				defAttrVals->destroy();
				info->destroy();
			}
			else {
				LOG_ERR << "failed to get rule file info";
				return false;
			}

			// new rpk is ready
			mRPKURI = nextRPKURI;
		}
	}

	return true;
}

bool SOP_PRT::updateParmsFlags() {
	for (std::string& p: mActiveParams[prt::AttributeMap::PT_FLOAT]) {
		double v = evalFloat(p.c_str(), 0, 0.0); // TODO: time
		std::wstring wp = utils::toUTF16FromOSNarrow(p);
		mAttributeSource->setFloat(wp.c_str(), v);
	}
	for (std::string& p: mActiveParams[prt::AttributeMap::PT_STRING]) {
		UT_String v;
		evalString(v, p.c_str(), 0, 0.0); // TODO: time
		std::wstring wp = utils::toUTF16FromOSNarrow(p);
		std::wstring wv = utils::toUTF16FromOSNarrow(v.toStdString());
		mAttributeSource->setString(wp.c_str(), wv.c_str());
	}

	forceRecook();

	bool changed = SOP_Node::updateParmsFlags();
	return changed;
}

void SOP_PRT::buildStartRuleMenu(void* data, PRM_Name* theMenu, int theMaxSize, const PRM_SpareData*, const PRM_Parm*) {
	SOP_PRT* node = static_cast<SOP_PRT*>(data);
//	LOG_DBG << "buildStartRuleMenu";
//	LOG_DBG << "   mRPKURI = " << node->mRPKURI;
//	LOG_DBG << "   mRuleFile = " << node->mRuleFile;

	if (node->mAssetsMap == nullptr || node->mRPKURI.empty() || node->mRuleFile.empty()) return;

	const wchar_t* cgbURI = node->mAssetsMap->getString(node->mRuleFile.c_str());
	if (cgbURI == nullptr) {
		LOG_ERR << L"failed to resolve rule file '" << node->mRuleFile << "', aborting.";
		return;
	}

	prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
	const prt::RuleFileInfo* rfi = prt::createRuleFileInfo(cgbURI, node->mPRTCache, &status);
	if (status == prt::STATUS_OK) {
		std::vector<std::pair<std::string,std::string>> startRules, rules;
		for (size_t ri = 0; ri < rfi->getNumRules() ; ri++) {
			const prt::RuleFileInfo::Entry* re = rfi->getRule(ri);
			std::string rn = utils::toOSNarrowFromUTF16(re->getName());
			std::vector<std::string> tok;
			boost::split(tok, rn, boost::is_any_of("$"));

			bool hasStartRuleAnnotation = false;
			for (size_t ai = 0; ai < re->getNumAnnotations(); ai++) {
				if (std::wcscmp(re->getAnnotation(ai)->getName(), L"@StartRule") == 0) {
					hasStartRuleAnnotation = true;
					break;
				}
			}

			if (hasStartRuleAnnotation)
				startRules.emplace_back(tok[1], tok[1] + " (@StartRule)");
			else
				rules.emplace_back(tok[1], tok[1]);
		}

		std::sort(startRules.begin(), startRules.end());
		std::sort(rules.begin(), rules.end());
		rules.reserve(rules.size() + startRules.size());
		rules.insert(rules.begin(), startRules.begin(), startRules.end());

		const size_t limit = std::min<size_t>(rules.size(), theMaxSize);
		for (size_t ri = 0; ri < limit; ri++) {
			theMenu[ri].setToken(rules[ri].first.c_str());
			theMenu[ri].setLabel(rules[ri].second.c_str()); // TODO: mark @StartRules
		}
		theMenu[limit].setToken(0); // need a null terminator
		rfi->destroy();
	}
}

} // namespace prt4hdn