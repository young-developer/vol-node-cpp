/* eslint-disable no-whitespace-before-property */

import { LAYOUT_COMMAND }           from './schema/SchemaBuilder';
import { barcodeToSVG }             from './util/pdf417';
import { fitText, JUSTIFY }         from './util/TextFitter';
import handlebars                   from 'handlebars';
import { observer }                 from 'mobx-react';
import React                        from 'react';

//================================================================//
// AssetView
//================================================================//
const AssetView = ( props ) => {

    const { inventory, assetId, inches } = props;

    const layout        = inventory.assetLayouts [ assetId ];

    const docX          = props.x || 0;
    const docY          = props.y || 0;

    const docWidth      = layout.width;
    const docHeight     = layout.height;
    const dpi           = layout.dpi;

    return (
        <svg
            x = { docX }
            y = { docY }
            width = { inches ? `${ docWidth / dpi }in` : docWidth }
            height = { inches ? `${ docHeight / dpi }in` : docHeight }
            viewBox = { `0 0 ${ docWidth } ${ docHeight }` }
            preserveAspectRatio = 'none'
        >
            <g dangerouslySetInnerHTML = {{ __html: layout.svg }}/>
        </svg>
    );
}

export default AssetView;