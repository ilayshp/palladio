#include "ShapeData.h"


namespace {

const std::wstring DEFAULT_SHAPE_NAME     = L"pldShape";
const std::wstring GROUP_NAME_LEGAL_CHARS = L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789";
const std::wstring INVALID_GROUP_NAME     = L"_invalid_";

/**
 * creates initial shape and primitive group name from primitive classifier value
 */
class NameFromPrimPart : public boost::static_visitor<> {
public:
	NameFromPrimPart(std::wstring& name, const std::wstring& prefix) : mName(name), mPrefix(prefix) { }

    void operator()(const UT_String& s) {
        mName.assign(toUTF16FromOSNarrow(s.toStdString()));
	    replace_all_not_of(mName, GROUP_NAME_LEGAL_CHARS);
	    if (mName.empty()) // handle empty primCls string value
		    mName.assign(INVALID_GROUP_NAME);
    }

    void operator()(const int32& i) {
        mName.assign(mPrefix).append(L"_").append(std::to_wstring(i));
	    replace_all_not_of(mName, GROUP_NAME_LEGAL_CHARS);
    }

private:
	std::wstring& mName;
	const std::wstring& mPrefix;
};

} // namespace


ShapeData::~ShapeData() {
	std::for_each(mInitialShapes.begin(), mInitialShapes.end(), [](const prt::InitialShape* is){
		if (is)
			is->destroy();
	});
}

void ShapeData::addBuilder(InitialShapeBuilderUPtr&& isb, const PrimitiveNOPtrVector& primMappings, const PrimitivePartition::ClassifierValueType& clsVal) {
	mInitialShapeBuilders.emplace_back(std::move(isb));
	mPrimitiveMapping.emplace_back(primMappings);

	if (mGroupCreation == GenerateNodeParams::GroupCreation::PRIMCLS) {
		std::wstring name;
		NameFromPrimPart npp(name, mNamePrefix);
		boost::apply_visitor(npp, clsVal);
		mInitialShapeNames.push_back(name);
	}
}

void ShapeData::addShape(const prt::InitialShape* is, AttributeMapBuilderUPtr&& amb, AttributeMapUPtr&& ruleAttr) {
	mInitialShapes.emplace_back(is);
	mRuleAttributeBuilders.emplace_back(std::move(amb));
	mRuleAttributes.emplace_back(std::move(ruleAttr));
}

const std::wstring& ShapeData::getInitialShapeName(size_t isIdx) const {
	if (mInitialShapeNames.empty()) {
		assert(mGroupCreation == GenerateNodeParams::GroupCreation::NONE);
		return DEFAULT_SHAPE_NAME;
	}
	else
		return mInitialShapeNames[isIdx];
}

bool ShapeData::isValid() const {
	const size_t numISB = mInitialShapeBuilders.size();
	const size_t numIS = mInitialShapes.size();
	const size_t numISN = mInitialShapeNames.size();

	const size_t numAMB = mRuleAttributeBuilders.size();
	const size_t numAM = mRuleAttributes.size();

	const size_t numPM = mPrimitiveMapping.size();

	if (numISB != numPM || (numISB != numISN && numISN > 0) || (numISB == 0 && numISN > 0))
		return false;

	if (numIS != numAMB || numIS != numAM) // they are allowed to be all 0
		return false;

	return true;
}
