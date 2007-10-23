/*******************************************************

   CoolReader Engine

   rtfimp.cpp:  RTF import implementation

   (c) Vadim Lopatin, 2000-2006
   This source code is distributed under the terms of
   GNU General Public License
   See LICENSE file for details

*******************************************************/
#include "../include/rtfimp.h"
#include "../include/crtxtenc.h"
#include <strings.h>

//==================================================
// RTF file parser


#undef RTF_CMD
#undef RTF_CHR
#undef RTF_CHC
#undef RTF_IPR
#undef RTF_DST
#define RTF_IPR( name, index, defvalue ) \
    { RTF_##name, #name, CWT_IPROP, index, defvalue },
#define RTF_CMD( name, type, index ) \
    { RTF_##name, #name, type, index, 0 },
#define RTF_CHC( name, index ) \
    { RTF_##name, #name, CWT_CHAR, index, 0 },
#define RTF_CHR( character, name, index ) \
    { RTF_##name, character, CWT_CHAR, index, 0 },
#define RTF_DST( name, index ) \
    { RTF_##name, #name, CWT_DEST, index, 0 },
static const rtf_control_word rtf_words[] = {
#include "../include/rtfcmd.h"
};
static const int rtf_words_count = sizeof(rtf_words) / sizeof(rtf_control_word);

static const rtf_control_word * findControlWord( const char * name )
{
    int a = 0;
    int b = rtf_words_count;
    int c;
    for ( ;; ) {
        if ( a >= b )
            return NULL;
        c = ( a + b ) / 2;
        //if ( c>=rtf_words_count ) {
        //    CRLog::error("findControlWord invalid index: %d of %d", c, rtf_words_count);
        //    return NULL;
        //}
        int res = strcmp( name, rtf_words[c].name );
        if ( !res )
            return &rtf_words[c];
        //if ( a + 1 >= b )
        //    return NULL;
        if ( res>0 )
            a = c + 1;
        else
            b = c;
    }
}

class LVRtfDefDestination : public LVRtfDestination
{
protected:
    bool in_section;
    bool in_title;
    bool in_para;
    bool last_space;
    bool last_notitle;
public:
    LVRtfDefDestination(  LVRtfParser & parser )
    : LVRtfDestination( parser )
    , in_section(false)
    , in_title(false)
    , in_para(false)
    , last_space(false)
    , last_notitle(true)
    {
    }
    virtual void OnControlWord( const char * control, int param )
    {
    }
    virtual void OnText( const lChar16 * text, int len,
        lvpos_t fpos, lvsize_t fsize, lUInt32 flags )
    {
        bool titleFlag = m_stack.getInt( pi_align )==ha_center;
        if ( last_notitle && titleFlag ) {
            OnAction(RA_SECTION);
        }
        if ( !in_section ) {
            m_callback->OnTagOpen(NULL, L"section");
            in_section = true;
        }
        if ( !in_title && titleFlag ) {
            m_callback->OnTagOpen(NULL, L"title");
            in_title = true;
            last_notitle = false;
        }
        if ( !in_para ) {
            if ( !in_title )
                last_notitle = true;
            m_callback->OnTagOpen(NULL, L"p");
            last_space = false;
            in_para = true;
        }
        if ( m_stack.getInt(pi_ch_bold) ) {
            m_callback->OnTagOpen(NULL, L"strong");
        }
        if ( m_stack.getInt(pi_ch_italic) ) {
            m_callback->OnTagOpen(NULL, L"emphasis");
        }

        lString16 s = text;
        s.trimDoubleSpaces(!last_space, true, false);
        text = s.c_str();
        len = s.length();
        if ( len ) {
            m_callback->OnText( text, len, fpos, fsize, flags );
            last_space = text[len-1]==' ';
        }

        if ( m_stack.getInt(pi_ch_italic) ) {
            m_callback->OnTagClose(NULL, L"emphasis");
        }
        if ( m_stack.getInt(pi_ch_bold) ) {
            m_callback->OnTagClose(NULL, L"strong");
        }
    }
    virtual void OnAction( int action )
    {
        if ( action==RA_PARA || action==RA_SECTION ) {
            if ( in_para ) {
                m_callback->OnTagClose(NULL, L"p");
                in_para = false;
            }
            if ( in_title ) {
                m_callback->OnTagClose(NULL, L"title");
                in_title = false;
            }
        }
        if ( action==RA_SECTION ) {
            if ( in_section ) {
                m_callback->OnTagClose(NULL, L"section");
                in_section = false;
            }
        }
    }
    virtual ~LVRtfDefDestination()
    {
        OnAction( RA_PARA );
        OnAction( RA_SECTION );
    }
};

class LVRtfNullDestination : public LVRtfDestination
{
public:
    LVRtfNullDestination(  LVRtfParser & parser )
    : LVRtfDestination( parser )
    {
    }
    virtual void OnControlWord( const char * control, int param )
    {
    }
    virtual void OnText( const lChar16 * text, int len,
        lvpos_t fpos, lvsize_t fsize, lUInt32 flags )
    {
    }
    virtual ~LVRtfNullDestination()
    {
    }
};


/// constructor
LVRtfParser::LVRtfParser( LVStreamRef stream, LVXMLParserCallback * callback )
    : LVFileParserBase(stream)
    , m_callback(callback)
    , m_stack( new LVRtfDefDestination(*this) )
    , txtbuf(NULL)
{
}

LVRtfDestination::LVRtfDestination( LVRtfParser & parser )
: m_parser(parser), m_stack( parser.getStack() ), m_callback( parser.getCallback() )
{
}

/// descructor
LVRtfParser::~LVRtfParser()
{
}

/// returns true if format is recognized by parser
bool LVRtfParser::CheckFormat()
{
    bool res = false;
    Reset();
    FillBuffer( 50 );
    res = (m_buf[0]=='{' && m_buf[1]=='\\' && m_buf[2]=='r'
         && m_buf[3]=='t' && m_buf[4]=='f' );
    Reset();
    return res;
}


void LVRtfParser::CommitText()
{
    if ( txtpos==0 )
        return;
    txtbuf[txtpos] = 0;
    if ( CRLog::isLogLevelEnabled(CRLog::LL_TRACE ) ) {
        lString16 s = txtbuf;
        lString8 s8 = UnicodeToUtf8( s );
        CRLog::trace( "Text(%s)", s8.c_str() );
    }
    m_stack.getDestination()->OnText( txtbuf, txtpos, txtfstart, (m_buf_fpos + m_buf_pos) - txtfstart, 0 );
    txtpos = 0;
}

#define MAX_TXT_SIZE 65535

void LVRtfParser::AddChar8( lUInt8 ch )
{
    AddChar( m_stack.byteToUnicode(ch) );
}

// m_buf_pos points to first byte of char
void LVRtfParser::AddChar( lChar16 ch )
{
    if ( txtpos >= MAX_TXT_SIZE || ch==13 ) {
        CommitText();
        m_stack.getDestination()->OnAction(LVRtfDestination::RA_PARA);
    }
    if ( txtpos==0 )
        txtfstart = m_buf_fpos + m_buf_pos;
    txtbuf[txtpos++] = ch;
}

#define MIN_BUF_DATA_SIZE 32768

static int charToHex( lUInt8 ch )
{
    if ( ch>='0' && ch<='9' )
        return ch-'0';
    if ( ch>='a' && ch<='f' )
        return ch-'a'+10;
    if ( ch>='A' && ch<='F' )
        return ch-'A'+10;
    return -1;
}

/// parses input stream
bool LVRtfParser::Parse()
{
    m_conv_table = GetCharsetByte2UnicodeTable( L"cp1251" );

    bool errorFlag = false;
    m_callback->OnStart(this);

    // make fb2 document structure
    m_callback->OnTagOpen( NULL, L"?xml" );
    m_callback->OnAttribute( NULL, L"version", L"1.0" );
    m_callback->OnAttribute( NULL, L"encoding", L"utf-8" );
    //m_callback->OnEncoding( GetEncodingName().c_str(), GetCharsetTable( ) );
    m_callback->OnTagClose( NULL, L"?xml" );
    m_callback->OnTagOpen( NULL, L"FictionBook" );
      // DESCRIPTION
      m_callback->OnTagOpen( NULL, L"description" );
        m_callback->OnTagOpen( NULL, L"title-info" );
          //
            lString16 bookTitle = getFileName(); //m_stream->GetName();
            m_callback->OnTagOpen( NULL, L"book-title" );
                if ( !bookTitle.empty() )
                    m_callback->OnText( bookTitle.c_str(), bookTitle.length(), 0, 0, 0 );
          //queue.DetectBookDescription( m_callback );
        m_callback->OnTagOpen( NULL, L"title-info" );
      m_callback->OnTagClose( NULL, L"description" );
      // BODY
      m_callback->OnTagOpen( NULL, L"body" );
        //m_callback->OnTagOpen( NULL, L"section" );
          // process text
          //queue.DoTextImport( m_callback );
        //m_callback->OnTagClose( NULL, L"section" );

    txtbuf = new lChar16[ MAX_TXT_SIZE+1 ];
    txtpos = 0;
    txtfstart = 0;
    char cwname[33];
    while ( !Eof() && !errorFlag && !m_stopped ) {
        // load next portion of data if necessary
        if ( m_buf_len - m_buf_pos < MIN_BUF_DATA_SIZE ) {
            if ( !FillBuffer( MIN_BUF_DATA_SIZE*2 ) ) {
                errorFlag = true;
                break;
            }
        }
        int len = (int)m_buf_len - (int)m_buf_pos;
        // check end of file
        if ( len <=0 )
            break; //EOF
        const lUInt8 * p = m_buf + m_buf_pos;
        lUInt8 ch = *p++;
        if ( ch=='{' ) {
            OnBraceOpen();
            m_buf_pos++;
            continue;
        } else if ( ch=='}' ) {
            OnBraceClose();
            m_buf_pos++;
            continue;
        }
        lUInt8 ch2 = *p;
        if ( ch=='\\' && *p!='\'' ) {
            // control
            bool asteriskFlag = false;
            if ( ch2=='*' ) {
                ch = *(++p);
                ch2 = *(++p);
                asteriskFlag = true;
            }
            if ( (ch2>='A' && ch2<='Z') || (ch2>='a' && ch2<='z') ) {
                // control word
                int cwi = 0;
                do {
                    cwname[cwi++] = ch2;
                    ch2 = *++p;
                } while ( ( (ch2>='A' && ch2<='Z') || (ch2>='a' && ch2<='z') ) && cwi<32 );
                cwname[cwi] = 0;
                int param = PARAM_VALUE_NONE;
                if ( ch2==' ' ) {
                    p++;
                } else {
                    if ( ch2 == '-' ) {
                        p++;
                        param = 0;
                        for ( ;; ) {
                            ch2 = *++p;
                            if ( ch2<'0' || ch2>'9' )
                                break;
                            param = param * 10 + (ch2-'0');
                        }
                        param = -param;
                    } else if ( ch2>='0' && ch2<='9' ) {
                        param = 0;
                        while ( ch2>='0' && ch2<='9' ) {
                            param = param * 10 + (ch2-'0');
                            ch2 = *++p;
                        }
                    }
                }
                // \uN -- unicode character
                if ( cwi==1 && cwname[0]=='u' ) {
                    AddChar( (lChar16) (param & 0xFFFF) );
                    m_stack.set( pi_skip_ansi, 1 );
                } else {
                    // usual control word
                    OnControlWord( cwname, param, asteriskFlag );
                }
            } else {
                // control char
                cwname[0] = ch2;
                cwname[1] = 0;
                p++;
                OnControlWord( cwname, PARAM_VALUE_NONE, asteriskFlag );
            }
            m_buf_pos += p - (m_buf + m_buf_pos);
        } else {
            lChar16 txtch = 0;
            if ( ch=='\\' ) {
                p++;
                int digit1 = charToHex( p[0] );
                int digit2 = charToHex( p[1] );
                p+=2;
                if ( digit1>=0 && digit2>=0 ) {
                    AddChar8( (lChar8)((digit1 << 4) | digit2) );
                } else {
                    AddChar('\\');
                    AddChar('\'');
                    AddChar8(digit1);
                    AddChar8(digit2);
                    p+=2;
                }
            } else {
                if ( ch>=' ' ) {

                    AddChar8( ch );
                } else {
                    // wrong char
                }
            }
            //=======================================================
            //=======================================================
            m_buf_pos += p - (m_buf + m_buf_pos);
        }
    }
    m_callback->OnStop();
    delete txtbuf;
    txtbuf = NULL;

    CommitText();
    m_stack.getDestination()->OnAction(LVRtfDestination::RA_SECTION);

      m_callback->OnTagClose( NULL, L"body" );
    m_callback->OnTagClose( NULL, L"FictionBook" );

    return !errorFlag;
}

/// resets parsing, moves to beginning of stream
void LVRtfParser::Reset()
{
    LVFileParserBase::Reset();
}

/// sets charset by name
void LVRtfParser::SetCharset( const lChar16 * name )
{
    //TODO
}

/// sets 8-bit charset conversion table (128 items, for codes 128..255)
void LVRtfParser::SetCharsetTable( const lChar16 * table )
{
    //TODO
}

/// returns 8-bit charset conversion table (128 items, for codes 128..255)
lChar16 * LVRtfParser::GetCharsetTable( )
{
    return NULL;
}

void LVRtfParser::OnBraceOpen()
{
    CommitText();
    m_stack.save();
}

void LVRtfParser::OnBraceClose()
{
    CommitText();
    m_stack.restore();
}

void LVRtfParser::OnControlWord( const char * control, int param, bool asterisk )
{
    const rtf_control_word * cw = findControlWord( control );
    if ( cw ) {
        switch ( cw->type ) {
        case CWT_CHAR:
            lChar16 ch = (lChar16)cw->index;
            if ( ch==13 ) {
                // TODO: end of paragraph
                CommitText();
                m_stack.getDestination()->OnAction(LVRtfDestination::RA_PARA);
            } else {
                AddChar(ch);
            }
            break;
        case CWT_STYLE:
            break;
        case CWT_DEST:
            CRLog::trace("DEST: \\%s", cw->name);
            switch ( cw->index ) {
            case dest_upr:
                m_stack.set( pi_skip_ansi, 1 );
                break;
            case dest_ud:
                m_stack.set( pi_skip_ansi, 0 );
                break;
            case dest_fonttbl:
                m_stack.set( new LVRtfNullDestination(*this) );
                break;
            case dest_stylesheet:
                m_stack.set( new LVRtfNullDestination(*this) );
                break;
            case dest_footnote:
                m_stack.set( new LVRtfNullDestination(*this) );
                break;
            case dest_info:
            case dest_header:
            case dest_footer:
            case dest_pict:
            case dest_colortbl:
                m_stack.set( new LVRtfNullDestination(*this) );
                break;
            }
            break;
        case CWT_IPROP:
            CommitText();
            if ( param == PARAM_VALUE_NONE )
                param = cw->defvalue;
            CRLog::trace("PROP: \\%s %d", cw->name, param);
            m_stack.set( cw->index, param );
            break;
        }
    } else {
        CRLog::trace("CW: %s\\%s %d", asterisk?"\\*":"", control, param==PARAM_VALUE_NONE ? 0 : param);
        if ( asterisk ) {
            // ignore text after unknown keyword
            m_stack.set( new LVRtfNullDestination(*this) );
            CRLog::trace("Ignoring unknown destination %s !!!", control );
        }
    }
}

