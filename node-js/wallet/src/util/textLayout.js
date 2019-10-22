/* eslint-disable no-whitespace-before-property */

import * as color               from './color';
import * as rect                from './rect';
import * as textStyle           from './textStyle';
import * as util                from './util';
import _                        from 'lodash';
import * as opentype            from 'opentype.js';

const WHITESPACE_CHAR_CODES = [
    ' '.charCodeAt ( 0 ),
    '\t'.charCodeAt ( 0 ),
    0,
];

// these are camelCase because they are used directly by SchemaBuilder. this is
// how they will appear in the schema JSON.
export const FONT_FACE = {
    REGULAR:        'regular',
    BOLD:           'bold',
    ITALIC:         'italic',
    BOLD_ITALIC:    'boldItalic',
};

export const JUSTIFY = {
    HORIZONTAL: {
        LEFT:       'LEFT',
        CENTER:     'CENTER',
        RIGHT:      'RIGHT',
    },
    VERTICAL: {
        TOP:        'TOP',
        CENTER:     'CENTER',
        BOTTOM:     'BOTTOM',
    },
}

const DEFAULT_MIN_SCALE_STEP        = 0.01;

const OPENTYPE_OPTIONS = {
    kerning:    true,
    features:   true,
    hinting:    false,
};

//----------------------------------------------------------------//
export const fitText = ( text, font, fontSize, x, y, width, height, hJustify, vJustify ) => {

    let fitter = new TextFitter ( x, y, width, height, vJustify || JUSTIFY.VERTICAL.TOP );
    fitter.pushSection ( text, font, fontSize, hJustify || JUSTIFY.HORIZONTAL.LEFT )
    fitter.fit ();
    return fitter.toSVG ();
}

//================================================================//
// TextLine
//================================================================//
class TextLine {

    //----------------------------------------------------------------//
    append ( tokenChars, context ) {

        this.tokenChars = this.tokenChars.concat ( tokenChars );

        let style       = false;
        let buffer      = '';
        let xOff        = 0;

        this.bounds     = false;
        this.segments   = [];
        this.height     = 0;

        const pushSegment = () => {

            const faces = context.fonts [ style.font ];
            if ( !faces ) return;

            let font = false;

            if (( style.bold ) && ( style.italic )) {
                font = faces [ FONT_FACE.BOLD_ITALIC ];
            }
            else if ( style.bold ) {
                font = faces [ FONT_FACE.BOLD ];
            }
            else if ( style.italic ) {
                font = faces [ FONT_FACE.ITALIC ];
            }

            font = font || faces [ FONT_FACE.REGULAR ];
            if ( !font ) return;

            const size      = style.size * style.scale * ( context.fontScale || 1 );
            const advance   = font.getAdvanceWidth ( buffer, size, OPENTYPE_OPTIONS );
            const path      = font.getPath ( buffer, xOff, 0, size, OPENTYPE_OPTIONS );
            let bb          = path.getBoundingBox ();
            bb              = rect.make ( bb.x1, bb.y1, bb.x2, bb.y2 );

            this.segments.push ({
                path:       path,
                style:      style,
                font:       font,
                bounds:     bb,
            });

            this.bounds = rect.grow ( this.bounds, bb );
            
            const ascender  = ( font.ascender / font.unitsPerEm ) * size;
            const descender  = -( font.descender / font.unitsPerEm ) * size;

            this.ascender = util.greater ( this.ascender, ascender );
            this.descender = util.greater ( this.descender, descender );

            xOff += advance;
        }

        const grow = () => {
            if ( buffer.length ) {
                pushSegment ();
            }
            buffer = '';
        }

        for ( let styledChar of this.tokenChars ) {

            if ( style != styledChar.style ) {
                grow ();
                style = styledChar.style;
            }
            buffer += styledChar.char;
        }
        grow ();
    }

    //----------------------------------------------------------------//
    constructor () {

        this.xOff       = 0;
        this.yOff       = 0;

        this.bounds         = false;
        this.tokenChars     = [];
        this.segments       = [];
        this.ascender       = 0;
        this.descender      = 0;
    }

    //----------------------------------------------------------------//
    makeSnapshot () {
        return {
            length:         this.tokenChars.length,
            bounds:         rect.copy ( this.bounds ),
            segments:       this.segments,
            ascender:       this.ascender,
            descender:      this.descender,
        };
    }

    //----------------------------------------------------------------//
    restoreFromSnapshot ( snapshot ) {
        this.tokenChars     = this.tokenChars.slice ( 0, snapshot.length );
        this.bounds         = snapshot.bounds;
        this.segments       = snapshot.segments;
        this.ascender       = snapshot.ascender;
        this.descender      = snapshot.descender;
    }

    //----------------------------------------------------------------//
    toSVG ( xOff, yOff ) {

        const x = this.xOff + ( xOff || 0 );
        const y = this.yOff + ( yOff || 0 );

        const paths = [];
        paths.push ( `<g transform = 'translate ( ${ x }, ${ y })'>` );

        for ( let segment of this.segments ) {
            const style = segment.style;
            const hexColor = color.toHexRGB ( style.color );

            paths.push ( `<g fill='${ hexColor }'>${ segment.path.toSVG ()}</g>` );
        }
        paths.push ( '</g>' );
        
        // return font.getPath ( this.text, this.xOff, this.yOff, fontSize ).toSVG ();
        return paths.join ( '' );
    }
}

//================================================================//
// TextBox
//================================================================//
export class TextBox {

    //----------------------------------------------------------------//
    constructor ( text, fonts, fontName, fontSize, x, y, width, height, hJustify, vJustify ) {

        this.text           = text;
        this.fonts          = fonts,

        this.bounds = rect.make (
            x,
            y,
            x + width,
            y + height,
        );

        this.hJustify = hJustify || JUSTIFY.HORIZONTAL.LEFT;
        this.vJustify = vJustify || JUSTIFY.VERTICAL.TOP;

        this.lines = [];

        this.baseStyle = {
            font:   fontName,
            size:   fontSize,
            color:  color.make ( 0, 0, 0, 1 ),
            scale:  1,
        }

        this.styledText     = textStyle.parse ( text, this.baseStyle );
    }

    //----------------------------------------------------------------//
    fit ( scale ) {

        this.lines = [];
        this.fitBounds = false;
        this.fontScale = scale || 1;

        const length = this.styledText.length;

        let tokenStart = 0;
        let inToken = false;

        for ( let i = 0; i <= length; ++i ) {

            const charCode = i < length ? this.styledText [ i ].char.charCodeAt ( 0 ) : 0;

            if ( WHITESPACE_CHAR_CODES.includes ( charCode )) {

                if ( inToken ) {
                    this.pushToken ( this.styledText.slice ( tokenStart, i ));
                    inToken = false;
                    tokenStart = i;
                }
            }
            else {
                inToken = true;
            }
        }

        if ( this.lines.length === 0 ) return false;
        
        // do the line layout
        let yOff = 0;
        for ( let i in this.lines ) {

            const line = this.lines [ i ];

            const lineLeft = -line.bounds.x0;
            const lineWidth = rect.width ( line.bounds );

            // horizontal layout
            switch ( this.hJustify ) {

                case JUSTIFY.HORIZONTAL.LEFT:
                    line.xOff = this.bounds.x0 + lineLeft;
                    break;

                case JUSTIFY.HORIZONTAL.CENTER:
                    line.xOff = ((( this.bounds.x0 + this.bounds.x1 ) - lineWidth ) / 2 ) + lineLeft;
                    break;
                
                case JUSTIFY.HORIZONTAL.RIGHT:
                    line.xOff = ( this.bounds.x1 - lineWidth ) + lineLeft;
                    break;
            }

            // vertical layout
            yOff += ( i == 0 ) ? -line.bounds.y0 : line.ascender;

            switch ( this.vJustify ) {

                case JUSTIFY.VERTICAL.TOP:
                    line.yOff = this.bounds.y0 + yOff;
                    break;

                case JUSTIFY.VERTICAL.CENTER: {
                    line.yOff = ((( this.bounds.y0 + this.bounds.y1 ) - ( y1 - y0 )) / 2 ) + yOff;
                    break;
                }
                case JUSTIFY.VERTICAL.BOTTOM:
                    line.yOff = ( this.bounds.x1 - ( y1 - y0 )) + yOff;
                    break;
            }
            yOff += line.descender;

            // update the bounds
            line.bounds = rect.offset ( line.bounds, line.xOff, line.yOff );
            this.fitBounds = rect.grow ( this.fitBounds, line.bounds );
        }

        const firstLine = _.first ( this.lines );
        this.padTop = firstLine.bounds.y0 - ( firstLine.yOff - firstLine.ascender );

        const lastLine = _.last ( this.lines );
        this.padBottom = ( lastLine.yOff + lastLine.descender ) - lastLine.bounds.y1;

        let hOverflow = ( rect.width ( this.bounds ) < rect.width ( this.fitBounds ));
        let vOverflow = ( rect.height ( this.bounds ) < rect.height ( this.fitBounds ));

        return ( hOverflow || vOverflow );
    }

    //----------------------------------------------------------------//
    pushToken ( token ) {

        if ( this.lines.length === 0 ) {
            this.lines = [ new TextLine ()];
        }

        const line = _.last ( this.lines );
        const snapshot = line.makeSnapshot ();
        const isNewLine = ( snapshot.length === 0 );

        if ( isNewLine ) {
            for ( let i = 0; i < token.length; ++i ) {
                const charCode = token [ i ].char.charCodeAt ( 0 );
                if ( WHITESPACE_CHAR_CODES.includes ( charCode ) === false ) {
                    token = token.slice ( i );
                    break;
                }
            }
        }

        if ( token.length === 0 ) return;

        line.append ( token, this );

        const bb = line.bounds;
        const over = bb ? rect.width ( this.bounds ) < rect.width ( bb ) : false;

        // only try new line if line was *not* originally empty
        if ( over && ( isNewLine === false )) {
            line.restoreFromSnapshot ( snapshot );
            this.lines.push ( new TextLine ());
            this.pushToken ( token );
        }
    }

    //----------------------------------------------------------------//
    toSVG ( xOff, yOff ) {

        if ( this.lines.length === 0 ) return '';

        let svg = [];
        for ( let i in this.lines ) {
            svg.push ( this.lines [ i ].toSVG ( xOff, yOff ));
        }
        return svg.join ();
    }
}


//================================================================//
// TextFitter
//================================================================//
export class TextFitter {

    //----------------------------------------------------------------//
    constructor ( fonts, x, y, width, height, vJustify ) {

        this.fonts = fonts;

        this.bounds = rect.make (
            x,
            y,
            x + width,
            y + height,
        );

        this.vJustify = vJustify || JUSTIFY.VERTICAL.TOP;

        this.sections = [];
    }

    //----------------------------------------------------------------//
    fit ( maxFontScale, minScaleStep ) {

        maxFontScale = ( typeof ( maxFontScale ) === 'number' ) ? maxFontScale : 1;
        minScaleStep = (( typeof ( minScaleStep ) === 'number' ) && ( minScaleStep > 0 )) ? minScaleStep : DEFAULT_MIN_SCALE_STEP;

        let fontScale = 1;
        let minFontScale = 0;
        let fitIterations = 0;

        const fitSections = () => {

            const maxHeight = rect.height ( this.bounds );
            let fitHeight = 0;
            let prevSection = false;

            for ( let i in this.sections ) {
                
                const section = this.sections [ i ];

                section.bounds = rect.copy ( this.bounds );
                const overflow = section.fit ( fontScale );
                if ( overflow ) return true;

                fitHeight += rect.height ( section.fitBounds );
                fitHeight += ( prevSection ) ? prevSection.padBottom + section.padTop : 0;
                
                if ( maxHeight < fitHeight ) return true;

                prevSection = section;
            }

            this.fitHeight = fitHeight;
            return false;
        }

        const fitRecurse = () => {

            fitIterations = fitIterations + 1;

            const overflow = fitSections ();

            if ( overflow ) {

                // always get smaller on overflow
                maxFontScale = fontScale;
                fontScale = ( minFontScale + maxFontScale ) / 2;
                fitRecurse ();
            }
            else {
                
                // no overflow. maybe get bigger.
                minFontScale = fontScale;
                let nextScale = ( maxFontScale > 0 ) ? ( minFontScale + maxFontScale ) / 2 : fontScale * 1.1;
                if (( nextScale - fontScale ) >= minScaleStep ) {
                    fontScale = nextScale;
                    fitRecurse ();
                }
            }
        }

        fitRecurse ();

        this.fontScale = fontScale;
        this.fitIterations = fitIterations;
    }

    //----------------------------------------------------------------//
    pushSection ( text, fontName, fontSize, hJustify ) {

        this.sections.push (
            new TextBox (
                text,
                this.fonts,
                fontName,
                fontSize,
                this.bounds.x0,
                this.bounds.y0,
                rect.width ( this.bounds ),
                rect.height ( this.bounds ),
                hJustify || JUSTIFY.HORIZONTAL.LEFT,
                JUSTIFY.VERTICAL.TOP
            )
        );
    }

    //----------------------------------------------------------------//
    toSVG () {

        if (( this.fitHeight === 0 ) || ( this.sections.length === 0 )) return '';

        let height = rect.height ( this.bounds );
        let fitHeight = this.fitHeight;
        let yOff = 0;

        switch ( this.vJustify ) {

            case JUSTIFY.VERTICAL.TOP:
                yOff = 0;
                break;

            case JUSTIFY.VERTICAL.CENTER: {
                yOff = ( height - fitHeight ) / 2;
                break;
            }
            case JUSTIFY.VERTICAL.BOTTOM:
                yOff = height - fitHeight;
                break;
        }

        let svg = [];
        for ( let i in this.sections ) {

            const section = this.sections [ i ];

            yOff += ( i == 0 ) ? 0 : section.padTop;
            svg.push ( section.toSVG ( 0, yOff ));
            yOff += rect.height ( section.fitBounds ) + section.padBottom;
        }

        return svg.join ();
    }
}
