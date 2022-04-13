#include "ScatterplotPlugin.h"
#include "ScatterplotWidget.h"
#include "DataHierarchyItem.h"
#include "Application.h"

#include "util/PixelSelectionTool.h"

#include "PointData.h"
#include "ClusterData.h"
#include "ColorData.h"

#include "graphics/Vector2f.h"
#include "graphics/Vector3f.h"
#include "widgets/DropWidget.h"

#include <Eigen/Eigen>

#include <QtCore>
#include <QApplication>
#include <QDebug>
#include <QMenu>
#include <QAction>
#include <QMetaType>

#include <algorithm>
#include <functional>
#include <limits>
#include <set>
#include <vector>
#include <iostream>

Q_PLUGIN_METADATA(IID "nl.uu.ExplanationScatterplot")

using namespace hdps;
using namespace hdps::util;

ScatterplotPlugin::ScatterplotPlugin(const PluginFactory* factory) :
    ViewPlugin(factory),
    _positionDataset(),
    _positionSourceDataset(),
    _positions(),
    _numPoints(0),
    _scatterPlotWidget(new ScatterplotWidget(_explanationModel)),
    _explanationWidget(new ExplanationWidget(_explanationModel)),
    _dropWidget(nullptr),
    _settingsAction(this),
    _selectionRadius(30),
    _lockSelection(false)
{
    _dropWidget = new DropWidget(_scatterPlotWidget);

    setDockingLocation(DockableWidget::DockingLocation::Right);
    setFocusPolicy(Qt::ClickFocus);

    connect(_scatterPlotWidget, &ScatterplotWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        if (!_positionDataset.isValid())
            return;

        auto contextMenu = _settingsAction.getContextMenu();
        
        contextMenu->addSeparator();

        _positionDataset->populateContextMenu(contextMenu);

        contextMenu->exec(mapToGlobal(point));
    });

    _dropWidget->setDropIndicatorWidget(new DropWidget::DropIndicatorWidget(this, "No data loaded", "Drag an item from the data hierarchy and drop it here to visualize data..."));
    _dropWidget->initialize([this](const QMimeData* mimeData) -> DropWidget::DropRegions {
        DropWidget::DropRegions dropRegions;

        const auto mimeText = mimeData->text();
        const auto tokens   = mimeText.split("\n");

        if (tokens.count() == 1)
            return dropRegions;

        const auto datasetGuiName       = tokens[0];
        const auto datasetId            = tokens[1];
        const auto dataType             = DataType(tokens[2]);
        const auto dataTypes            = DataTypes({ PointType , ColorType, ClusterType });

        // Check if the data type can be dropped
        if (!dataTypes.contains(dataType))
            dropRegions << new DropWidget::DropRegion(this, "Incompatible data", "This type of data is not supported", "exclamation-circle", false);

        // Points dataset is about to be dropped
        if (dataType == PointType) {

            // Get points dataset from the core
            auto candidateDataset = _core->requestDataset<Points>(datasetId);

            // Establish drop region description
            const auto description = QString("Visualize %1 as points or density/contour map").arg(datasetGuiName);

            if (!_positionDataset.isValid()) {

                // Load as point positions when no dataset is currently loaded
                dropRegions << new DropWidget::DropRegion(this, "Point position", description, "map-marker-alt", true, [this, candidateDataset]() {
                    _positionDataset = candidateDataset;
                });
            }
            else {
                if (_positionDataset != candidateDataset && candidateDataset->getNumDimensions() >= 2) {

                    // The number of points is equal, so offer the option to replace the existing points dataset
                    dropRegions << new DropWidget::DropRegion(this, "Point position", description, "map-marker-alt", true, [this, candidateDataset]() {
                        _positionDataset = candidateDataset;
                    });
                }

                if (candidateDataset->getNumPoints() == _positionDataset->getNumPoints()) {

                    // The number of points is equal, so offer the option to use the points dataset as source for points colors
                    dropRegions << new DropWidget::DropRegion(this, "Point color", QString("Colorize %1 points with %2").arg(_positionDataset->getGuiName(), candidateDataset->getGuiName()), "palette", true, [this, candidateDataset]() {
                        _settingsAction.getColoringAction().addColorDataset(candidateDataset);
                        _settingsAction.getColoringAction().setCurrentColorDataset(candidateDataset);
                    });

                    // The number of points is equal, so offer the option to use the points dataset as source for points size
                    dropRegions << new DropWidget::DropRegion(this, "Point size", QString("Size %1 points with %2").arg(_positionDataset->getGuiName(), candidateDataset->getGuiName()), "ruler-horizontal", true, [this, candidateDataset]() {
                        _settingsAction.getPlotAction().getPointPlotAction().addPointSizeDataset(candidateDataset);
                        _settingsAction.getPlotAction().getPointPlotAction().getSizeAction().setCurrentDataset(candidateDataset);
                    });

                    // The number of points is equal, so offer the option to use the points dataset as source for points opacity
                    dropRegions << new DropWidget::DropRegion(this, "Point opacity", QString("Set %1 points opacity with %2").arg(_positionDataset->getGuiName(), candidateDataset->getGuiName()), "brush", true, [this, candidateDataset]() {
                        _settingsAction.getPlotAction().getPointPlotAction().addPointOpacityDataset(candidateDataset);
                        _settingsAction.getPlotAction().getPointPlotAction().getOpacityAction().setCurrentDataset(candidateDataset);
                    });
                }
            }
        }

        // Cluster dataset is about to be dropped
        if (dataType == ClusterType) {

            // Get clusters dataset from the core
            auto candidateDataset  = _core->requestDataset<Clusters>(datasetId);
            
            // Establish drop region description
            const auto description  = QString("Color points by %1").arg(candidateDataset->getGuiName());

            // Only allow user to color by clusters when there is a positions dataset loaded
            if (_positionDataset.isValid()) {

                if (_settingsAction.getColoringAction().hasColorDataset(candidateDataset)) {

                    // The clusters dataset is already loaded
                    dropRegions << new DropWidget::DropRegion(this, "Color", description, "palette", true, [this, candidateDataset]() {
                        _settingsAction.getColoringAction().setCurrentColorDataset(candidateDataset);
                    });
                }
                else {

                    // Use the clusters set for points color
                    dropRegions << new DropWidget::DropRegion(this, "Color", description, "palette", true, [this, candidateDataset]() {
                        _settingsAction.getColoringAction().addColorDataset(candidateDataset);
                        _settingsAction.getColoringAction().setCurrentColorDataset(candidateDataset);
                    });
                }
            }
            else {

                // Only allow user to color by clusters when there is a positions dataset loaded
                dropRegions << new DropWidget::DropRegion(this, "No points data loaded", "Clusters can only be visualized in concert with points data", "exclamation-circle", false);
            }
        }

        return dropRegions;
    });

    _scatterPlotWidget->installEventFilter(this);

    connect(_explanationWidget->getRadiusSlider(), &QSlider::valueChanged, this, &ScatterplotPlugin::neighbourhoodRadiusValueChanged);
    connect(_explanationWidget->getRadiusSlider(), &QSlider::sliderPressed, this, &ScatterplotPlugin::neighbourhoodRadiusSliderPressed);
    connect(_explanationWidget->getRadiusSlider(), &QSlider::sliderReleased, this, &ScatterplotPlugin::neighbourhoodRadiusSliderReleased);
    connect(&_explanationModel, &ExplanationModel::explanationMetricChanged, this, &ScatterplotPlugin::explanationMetricChanged);
    connect(&_explanationModel, &ExplanationModel::datasetDimensionsChanged, this, &ScatterplotPlugin::datasetDimensionsChanged);
    connect(&_explanationWidget->getBarchart(), &BarChart::dimensionExcluded, &_explanationModel, &ExplanationModel::excludeDimension);
    //connect(_explanationWidget->getRankingComboBox(), &QComboBox::currentIndexChanged, this, &ScatterplotPlugin::dimensionRankingChanged);
    //connect(_explanationWidget->getVarianceColoringButton(), &QPushButton::pressed, this, &ScatterplotPlugin::colorByVariance);
    //connect(_explanationWidget->getValueColoringButton(), &QPushButton::pressed, this, &ScatterplotPlugin::colorByValue);
}

ScatterplotPlugin::~ScatterplotPlugin()
{
}

void ScatterplotPlugin::init()
{
    auto layout = new QVBoxLayout();

    layout->setMargin(0);
    layout->setSpacing(0);
    auto settingsWidget = _settingsAction.createWidget(this);
    settingsWidget->setMaximumHeight(40);
    layout->addWidget(settingsWidget);

    auto centralWidget = new QWidget();
    centralWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto centralLayout = new QHBoxLayout();
    centralLayout->addWidget(_scatterPlotWidget);
    centralLayout->addWidget(_explanationWidget);
    centralWidget->setLayout(centralLayout);
    layout->addWidget(centralWidget);

    auto bottomToolbarWidget = new QWidget();
    auto bottomToolbarLayout = new QHBoxLayout();

    bottomToolbarWidget->setAutoFillBackground(true);
    bottomToolbarWidget->setLayout(bottomToolbarLayout);
    bottomToolbarWidget->setMaximumHeight(40);
    bottomToolbarLayout->setMargin(4);
    bottomToolbarLayout->addWidget(_settingsAction.getPlotAction().getPointPlotAction().getFocusSelection().createWidget(this));
    bottomToolbarLayout->addStretch(0);
    bottomToolbarLayout->addWidget(_settingsAction.getExportAction().createWidget(this));
    bottomToolbarLayout->addWidget(_settingsAction.getMiscellaneousAction().createCollapsedWidget(this));

    layout->addWidget(bottomToolbarWidget, 1);

    setLayout(layout);

    // Update the data when the scatter plot widget is initialized
    connect(_scatterPlotWidget, &ScatterplotWidget::initialized, this, &ScatterplotPlugin::updateData);

    // Update the selection when the pixel selection tool selected area changed
    connect(&_scatterPlotWidget->getPixelSelectionTool(), &PixelSelectionTool::areaChanged, [this]() {
        if (_scatterPlotWidget->getPixelSelectionTool().isNotifyDuringSelection())
            selectPoints();
    });

    // Update the selection when the pixel selection process ended
    connect(&_scatterPlotWidget->getPixelSelectionTool(), &PixelSelectionTool::ended, [this]() {
        if (_scatterPlotWidget->getPixelSelectionTool().isNotifyDuringSelection())
            return;

        selectPoints();
    });

    registerDataEventByType(PointType, std::bind(&ScatterplotPlugin::onDataEvent, this, std::placeholders::_1));

    // Load points when the pointer to the position dataset changes
    connect(&_positionDataset, &Dataset<Points>::changed, this, &ScatterplotPlugin::positionDatasetChanged);

    // Update points when the position dataset data changes
    connect(&_positionDataset, &Dataset<Points>::dataChanged, this, &ScatterplotPlugin::updateData);

    // Update point selection when the position dataset data changes
    connect(&_positionDataset, &Dataset<Points>::dataSelectionChanged, this, &ScatterplotPlugin::updateSelection);

    // Update the window title when the GUI name of the position dataset changes
    connect(&_positionDataset, &Dataset<Points>::dataGuiNameChanged, this, &ScatterplotPlugin::updateWindowTitle);

    // Do an initial update of the window title
    updateWindowTitle();
}

void ScatterplotPlugin::onDataEvent(hdps::DataEvent* dataEvent)
{
    if (dataEvent->getType() == EventType::DataSelectionChanged)
    {
        if (dataEvent->getDataset() == _positionDataset)
        {
            if (_positionDataset->isDerivedData())
            {
                hdps::Dataset<Points> sourceDataset = _positionDataset->getSourceDataset<Points>();
                hdps::Dataset<Points> selection = sourceDataset->getSelection();

                std::vector<float> dimRanking(sourceDataset->getNumDimensions());

                if (selection->indices.size() > 0)
                {
                    _explanationModel.computeDimensionRanks(dimRanking, selection->indices);
                }

                _explanationWidget->getBarchart().setRanking(dimRanking, selection->indices);
                //std::vector<float> importantDims;
                //QImage image = _explanation.computeEigenImage(selection->indices, importantDims);

                //_explanationWidget->getBarchart().setRanking(dimRanking, selection->indices);
                //_explanationWidget->getImageWidget().setImage(image);

                //image.save(QString("eigImage%1.png").arg(0));
                //////////////////
                //std::vector<float> pixels(numDimensions, 0);
                //for (int i = 0; i < eigenVectors.size(); i++)
                //{
                //    const std::vector<float>& eigenVector = eigenVectors[i];
                //    
                //    QImage image(28, 28, QImage::Format::Format_ARGB32);
                //    for (int d = 0; d < numDimensions; d++)
                //    {
                //        int value = (int) (fabs(eigenVector[d]) * 255 * (sortedEigenValues[sortedIndices[i]] / eigenValueSum) * 10);
                //        image.setPixel(d % 28, d / 28, qRgba(value, value, value, 255));
                //    }
                //    image.save(QString("eigImage%1.png").arg(i));
                //}
                //std::cout << "Saved eigen vectors" << std::endl;
            }
        }
    }
}

void ScatterplotPlugin::neighbourhoodRadiusValueChanged(int value)
{
    _scatterPlotWidget->setNeighbourhoodRadius(value / 100.0f);
    _explanationModel.recomputeNeighbourhood(value / 100.0f);

    colorPointsByRanking();
}

void ScatterplotPlugin::neighbourhoodRadiusSliderPressed()
{
    _scatterPlotWidget->drawNeighbourhoodRadius(true);
}

void ScatterplotPlugin::neighbourhoodRadiusSliderReleased()
{
    _scatterPlotWidget->drawNeighbourhoodRadius(false);
}

void ScatterplotPlugin::explanationMetricChanged()
{
    colorPointsByRanking();
}

void ScatterplotPlugin::datasetDimensionsChanged()
{
    std::cout << "Dim excluded" << std::endl;
    colorPointsByRanking();
}

void ScatterplotPlugin::colorPointsByRanking()
{
    _explanationModel.recomputeMetrics();

    Eigen::ArrayXXf dimRanking;
    _explanationModel.computeDimensionRanks(dimRanking);

    _explanationModel.recomputeColorMapping(dimRanking);

    hdps::Dataset<Points> sourceDataset = _positionDataset->getSourceDataset<Points>();
    hdps::Dataset<Points> selection = sourceDataset->getSelection();
    _explanationWidget->getBarchart().setRanking(dimRanking, selection->indices);

    std::vector<float> confidences = _explanationModel.computeConfidences(dimRanking);

    // Build vector of top ranked dimensions
    std::vector<int> topRankedDims(dimRanking.rows());
    
    const DataTable& dataset = _explanationModel.getDataset();
    for (int i = 0; i < dimRanking.rows(); i++)
    {
        std::vector<int> indices(dimRanking.cols());
        std::iota(indices.begin(), indices.end(), 0); //Initializing

        if (_explanationModel.currentMetric() == Explanation::Metric::VARIANCE)
            std::sort(indices.begin(), indices.end(), [&](int a, int b) {return dimRanking(i, a) < dimRanking(i, b); });
        else
            std::sort(indices.begin(), indices.end(), [&](int a, int b) {return dimRanking(i, a) > dimRanking(i, b); });

        int j = 0;
        while (dataset.isExcluded(indices[j]) && j < dataset.numDimensions()-1) { j++; }
        topRankedDims[i] = indices[j];
    }

    // Color points by dimension ranking
    const std::vector<QColor>& colorMapping = _explanationModel.getColorMapping();

    std::vector<Vector3f> colorData(topRankedDims.size());
    for (int i = 0; i < topRankedDims.size(); i++)
    {
        int dim = topRankedDims[i];
        float confidence = confidences[i];

        if (dim < colorMapping.size())
        {
            QColor color = colorMapping[dim];
            colorData[i] = Vector3f(color.redF() * confidence, color.greenF() * confidence, color.blueF() * confidence);
        }
        else
            colorData[i] = Vector3f(1.0f * confidence, 0.2f * confidence, 0.2f * confidence);
    }

    _scatterPlotWidget->setColors(colorData);
}

void ScatterplotPlugin::loadData(const Datasets& datasets)
{
    // Exit if there is nothing to load
    if (datasets.isEmpty())
        return;

    // Load the first dataset
    _positionDataset = datasets.first();

    // And set the coloring mode to constant
    _settingsAction.getColoringAction().getColorByAction().setCurrentIndex(0);
}

void ScatterplotPlugin::createSubset(const bool& fromSourceData /*= false*/, const QString& name /*= ""*/)
{
    auto subsetPoints = _positionDataset->getSourceDataset<Points>();

    // Create the subset
    auto subset = subsetPoints->createSubset(_positionDataset->getGuiName(), _positionDataset);

    // Notify others that the subset was added
    _core->notifyDataAdded(subset);
    
    // And select the subset
    subset->getDataHierarchyItem().select();
}

void ScatterplotPlugin::selectPoints()
{
    // Only proceed with a valid points position dataset and when the pixel selection tool is active
    if (!_positionDataset.isValid() || !_scatterPlotWidget->getPixelSelectionTool().isActive())
        return;

    // Get binary selection area image from the pixel selection tool
    auto selectionAreaImage = _scatterPlotWidget->getPixelSelectionTool().getAreaPixmap().toImage();

    // Get smart pointer to the position selection dataset
    auto selectionSet = _positionDataset->getSelection<Points>();

    // Create vector for target selection indices
    std::vector<std::uint32_t> targetSelectionIndices;

    // Reserve space for the indices
    targetSelectionIndices.reserve(_positionDataset->getNumPoints());

    // Mapping from local to global indices
    std::vector<std::uint32_t> localGlobalIndices;

    // Get global indices from the position dataset
    _positionDataset->getGlobalIndices(localGlobalIndices);

    const auto dataBounds   = _scatterPlotWidget->getBounds();
    const auto width        = selectionAreaImage.width();
    const auto height       = selectionAreaImage.height();
    const auto size         = width < height ? width : height;

    // Loop over all points and establish whether they are selected or not
    for (std::uint32_t i = 0; i < _positions.size(); i++) {
        const auto uvNormalized     = QPointF((_positions[i].x - dataBounds.getLeft()) / dataBounds.getWidth(), (dataBounds.getTop() - _positions[i].y) / dataBounds.getHeight());
        const auto uvOffset         = QPoint((selectionAreaImage.width() - size) / 2.0f, (selectionAreaImage.height() - size) / 2.0f);
        const auto uv               = uvOffset + QPoint(uvNormalized.x() * size, uvNormalized.y() * size);

        // Add point if the corresponding pixel selection is on
        if (selectionAreaImage.pixelColor(uv).alpha() > 0)
            targetSelectionIndices.push_back(localGlobalIndices[i]);
    }

    switch (_scatterPlotWidget->getPixelSelectionTool().getModifier())
    {
        case PixelSelectionModifierType::Replace:
            break;

        case PixelSelectionModifierType::Add:
        case PixelSelectionModifierType::Remove:
        {
            // Get reference to the indices of the selection set
            auto& selectionSetIndices = selectionSet->indices;

            // Create a set from the selection set indices
            QSet<std::uint32_t> set(selectionSetIndices.begin(), selectionSetIndices.end());

            switch (_scatterPlotWidget->getPixelSelectionTool().getModifier())
            {
                // Add points to the current selection
                case PixelSelectionModifierType::Add:
                {
                    // Add indices to the set 
                    for (const auto& targetIndex : targetSelectionIndices)
                        set.insert(targetIndex);

                    break;
                }

                // Remove points from the current selection
                case PixelSelectionModifierType::Remove:
                {
                    // Remove indices from the set 
                    for (const auto& targetIndex : targetSelectionIndices)
                        set.remove(targetIndex);

                    break;
                }

                default:
                    break;
            }

            // Convert set back to vector
            targetSelectionIndices = std::vector<std::uint32_t>(set.begin(), set.end());

            break;
        }

        default:
            break;
    }

    // Apply the selection indices
    _positionDataset->setSelectionIndices(targetSelectionIndices);

    // Notify others that the selection changed
    _core->notifyDataSelectionChanged(_positionDataset);
}

void ScatterplotPlugin::updateWindowTitle()
{
    if (!_positionDataset.isValid())
        setWindowTitle(getGuiName());
    else
        setWindowTitle(QString("%1: %2").arg(getGuiName(), _positionDataset->getDataHierarchyItem().getFullPathName()));
}

Dataset<Points>& ScatterplotPlugin::getPositionDataset()
{
    return _positionDataset;
}

Dataset<Points>& ScatterplotPlugin::getPositionSourceDataset()
{
    return _positionSourceDataset;
}

void ScatterplotPlugin::positionDatasetChanged()
{
    // Only proceed if we have a valid position dataset
    if (!_positionDataset.isValid())
        return;

    // Reset dataset references
    _positionSourceDataset.reset();

    // Set position source dataset reference when the position dataset is derived
    if (_positionDataset->isDerivedData())
        _positionSourceDataset = _positionDataset->getSourceDataset<Points>();

    // Enable pixel selection if the point positions dataset is valid
    _scatterPlotWidget->getPixelSelectionTool().setEnabled(_positionDataset.isValid());

    // Do not show the drop indicator if there is a valid point positions dataset
    _dropWidget->setShowDropIndicator(!_positionDataset.isValid());

    // Update positions data
    updateData();

    // Compute explanations
    _explanationModel.setDataset(_positionDataset->getSourceDataset<Points>(), _positionDataset);
    _explanationModel.recomputeNeighbourhood(0.1f);

    colorPointsByRanking();

    // Update the window title to reflect the position dataset change
    updateWindowTitle();
}

void ScatterplotPlugin::loadColors(const Dataset<Points>& points, const std::uint32_t& dimensionIndex)
{
    // Only proceed with valid points dataset
    if (!points.isValid())
        return;

    // Generate point scalars for color mapping
    std::vector<float> scalars;

    if (_positionDataset->getNumPoints() != _numPoints)
    {
        qWarning("Number of points used for coloring does not match number of points in data, aborting attempt to color plot");
        return;
    }

    // Populate point scalars
    if (dimensionIndex >= 0)
        points->extractDataForDimension(scalars, dimensionIndex);

    // Assign scalars and scalar effect
    _scatterPlotWidget->setScalars(scalars);
    _scatterPlotWidget->setScalarEffect(PointEffect::Color);

    // Render
    update();
}

void ScatterplotPlugin::loadColors(const Dataset<Clusters>& clusters)
{
    // Only proceed with valid clusters and position dataset
    if (!clusters.isValid() || !_positionDataset.isValid())
        return;

    // Mapping from local to global indices
    std::vector<std::uint32_t> globalIndices;

    // Get global indices from the position dataset
    _positionDataset->getGlobalIndices(globalIndices);

    // Generate color buffer for global and local colors
    std::vector<Vector3f> globalColors(globalIndices.back() + 1);
    std::vector<Vector3f> localColors(_positions.size());

    // Loop over all clusters and populate global colors
    for (const auto& cluster : clusters->getClusters())
        for (const auto& index : cluster.getIndices())
            globalColors[globalIndices[index]] = Vector3f(cluster.getColor().redF(), cluster.getColor().greenF(), cluster.getColor().blueF());

    std::int32_t localColorIndex = 0;

    // Loop over all global indices and find the corresponding local color
    for (const auto& globalIndex : globalIndices)
        localColors[localColorIndex++] = globalColors[globalIndex];

    // Apply colors to scatter plot widget without modification
    _scatterPlotWidget->setColors(localColors);

    // Render
    update();
}

ScatterplotWidget& ScatterplotPlugin::getScatterplotWidget()
{
    return *_scatterPlotWidget;
}

void ScatterplotPlugin::updateData()
{
    // Check if the scatter plot is initialized, if not, don't do anything
    if (!_scatterPlotWidget->isInitialized())
        return;
    
    // If no dataset has been selected, don't do anything
    if (_positionDataset.isValid()) {

        // Get the selected dimensions to use as X and Y dimension in the plot
        const auto xDim = _settingsAction.getPositionAction().getDimensionX();
        const auto yDim = _settingsAction.getPositionAction().getDimensionY();

        // If one of the dimensions was not set, do not draw anything
        if (xDim < 0 || yDim < 0)
            return;

        // Determine number of points depending on if its a full dataset or a subset
        _numPoints = _positionDataset->getNumPoints();

        // Extract 2-dimensional points from the data set based on the selected dimensions
        calculatePositions(*_positionDataset);

        // Pass the 2D points to the scatter plot widget
        _scatterPlotWidget->setData(&_positions);

        updateSelection();
    }
    else {
        _positions.clear();
        _scatterPlotWidget->setData(&_positions);
    }
}

void ScatterplotPlugin::calculatePositions(const Points& points)
{
    points.extractDataForDimensions(_positions, _settingsAction.getPositionAction().getDimensionX(), _settingsAction.getPositionAction().getDimensionY());
}

void ScatterplotPlugin::updateSelection()
{
    if (!_positionDataset.isValid())
        return;

    auto selection = _positionDataset->getSelection<Points>();

    std::vector<bool> selected;
    std::vector<char> highlights;

    _positionDataset->selectedLocalIndices(selection->indices, selected);

    highlights.resize(_positionDataset->getNumPoints(), 0);

    for (int i = 0; i < selected.size(); i++)
        highlights[i] = selected[i] ? 1 : 0;

    _scatterPlotWidget->setHighlights(highlights, static_cast<std::int32_t>(selection->indices.size()));
}

std::uint32_t ScatterplotPlugin::getNumberOfPoints() const
{
    if (!_positionDataset.isValid())
        return 0;

    return _positionDataset->getNumPoints();
}

void ScatterplotPlugin::setXDimension(const std::int32_t& dimensionIndex)
{
    updateData();
}

void ScatterplotPlugin::setYDimension(const std::int32_t& dimensionIndex)
{
    updateData();
}

void ScatterplotPlugin::computeLensSelection(std::vector<std::uint32_t>& targetSelectionIndices)
{
    // Get smart pointer to the position selection dataset
    auto selectionSet = _positionDataset->getSelection<Points>();

    // Reserve space for the indices
    targetSelectionIndices.reserve(_positionDataset->getNumPoints());

    // Mapping from local to global indices
    std::vector<std::uint32_t> localGlobalIndices;

    // Get global indices from the position dataset
    _positionDataset->getGlobalIndices(localGlobalIndices);

    const auto dataBounds = _scatterPlotWidget->getBounds();
    const auto w = _scatterPlotWidget->width();
    const auto h = _scatterPlotWidget->height();
    const auto size = w < h ? w : h;

    // Loop over all points and establish whether they are selected or not
    for (std::uint32_t i = 0; i < _positions.size(); i++) {
        const auto uvNormalized = QPointF((_positions[i].x - dataBounds.getLeft()) / dataBounds.getWidth(), (dataBounds.getTop() - _positions[i].y) / dataBounds.getHeight());
        const auto uvOffset = QPoint((_scatterPlotWidget->width() - size) / 2.0f, (_scatterPlotWidget->height() - size) / 2.0f);
        const auto uv = uvOffset + QPoint(uvNormalized.x() * size, uvNormalized.y() * size);

        //// Add point if the corresponding pixel selection is on
        //if (selectionAreaImage.pixelColor(uv).alpha() > 0)

        //QPoint mouseUV((2 * mouseEvent->x() / _scatterPlotWidget->width()) - 1, (2 * mouseEvent->y() / _scatterPlotWidget->height()) - 1);
        QPoint diff = QPoint(_lastMousePos.x(), _lastMousePos.y()) - uv;
        //float len = sqrt(pow(diff.x(), 2) + pow(diff.y(), 2));
        //qDebug() << mouseUV << uv << len;
        if (sqrt(diff.x() * diff.x() + diff.y() * diff.y()) < _selectionRadius)
            targetSelectionIndices.push_back(localGlobalIndices[i]);
    }
}

bool ScatterplotPlugin::eventFilter(QObject* target, QEvent* event)
{
    auto shouldPaint = false;

    switch (event->type())
    {
    case QEvent::KeyPress:
    {
        // Get key that was pressed
        auto keyEvent = static_cast<QKeyEvent*>(event);

        if (keyEvent->key() == Qt::Key_Control)
        {
            _lockSelection = true;

            if (_positionDataset.isValid())
            {
                _explanationWidget->getBarchart().computeOldMetrics(_positionDataset->getSelection()->getSelectionIndices());

                _explanationWidget->getBarchart().showDifferentialValues(true);

                // Draw little circle
                _scatterPlotWidget->setOldPosition(_lastMousePos, _selectionRadius);
                _scatterPlotWidget->drawOldSelection(true);
            }
        }

        break;
    }
    case QEvent::KeyRelease:
    {
        // Get key that was pressed
        auto keyEvent = static_cast<QKeyEvent*>(event);

        if (keyEvent->key() == Qt::Key_Control)
        {
            _lockSelection = false;
            _explanationWidget->getBarchart().showDifferentialValues(false);
            _scatterPlotWidget->drawOldSelection(false);
        }

        break;
    }

    case QEvent::Resize:
    {
        const auto resizeEvent = static_cast<QResizeEvent*>(event);

        //_shapePixmap = QPixmap(resizeEvent->size());
        //_areaPixmap = QPixmap(resizeEvent->size());

        //_shapePixmap.fill(Qt::transparent);
        //_areaPixmap.fill(Qt::transparent);

        //shouldPaint = true;

        break;
    }

    case QEvent::MouseButtonPress:
    {
        auto mouseEvent = static_cast<QMouseEvent*>(event);

        qDebug() << "Mouse button press";

        _mousePressed = true;

        break;

        //_mouseButtons = mouseEvent->buttons();

        //switch (_type)
        //{
        //case PixelSelectionType::Rectangle:
        //case PixelSelectionType::Brush:
        //case PixelSelectionType::Lasso:
        //case PixelSelectionType::Sample:
        //{
        //    switch (mouseEvent->button())
        //    {
        //    case Qt::LeftButton:
        //        startSelection();
        //        _mousePositions.clear();
        //        _mousePositions << mouseEvent->pos();
        //        break;

        //    case Qt::RightButton:
        //        break;

        //    default:
        //        break;
        //    }        
    }

    case QEvent::MouseButtonRelease:
    {
        auto mouseEvent = static_cast<QMouseEvent*>(event);

        _mousePressed = false;

        break;
    }

    case QEvent::MouseMove:
    {
        auto mouseEvent = static_cast<QMouseEvent*>(event);

        if (!_mousePressed)
            break;

        _lastMousePos = QPoint(mouseEvent->x(), mouseEvent->y());

        _scatterPlotWidget->setCurrentPosition(_lastMousePos, _selectionRadius);

        if (!_positionDataset.isValid())
            return QObject::eventFilter(target, event);

        // Create vector for target selection indices
        std::vector<std::uint32_t> targetSelectionIndices;
        computeLensSelection(targetSelectionIndices);

        // Apply the selection indices
        _positionDataset->setSelectionIndices(targetSelectionIndices);

        // Notify others that the selection changed
        _core->notifyDataSelectionChanged(_positionDataset);

        _explanationWidget->update();

        break;
    }

    case QEvent::Wheel:
    {
        auto wheelEvent = static_cast<QWheelEvent*>(event);

        _selectionRadius += wheelEvent->angleDelta().y() > 0 ? 2 : -2;
        if (_selectionRadius < 2) _selectionRadius = 2;

        _scatterPlotWidget->setCurrentPosition(_lastMousePos, _selectionRadius);

        if (!_positionDataset.isValid())
            return QObject::eventFilter(target, event);

        // Create vector for target selection indices
        std::vector<std::uint32_t> targetSelectionIndices;
        computeLensSelection(targetSelectionIndices);

        // Apply the selection indices
        _positionDataset->setSelectionIndices(targetSelectionIndices);

        // Notify others that the selection changed
        _core->notifyDataSelectionChanged(_positionDataset);

        _explanationWidget->update();

        break;
    }

    default:
        break;
    }

    return QObject::eventFilter(target, event);
}

QIcon ScatterplotPluginFactory::getIcon() const
{
    return Application::getIconFont("FontAwesome").getIcon("braille");
}

ViewPlugin* ScatterplotPluginFactory::produce()
{
    return new ScatterplotPlugin(this);
}

hdps::DataTypes ScatterplotPluginFactory::supportedDataTypes() const
{
    DataTypes supportedTypes;
    supportedTypes.append(PointType);
    return supportedTypes;
}
