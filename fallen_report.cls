\NeedsTeXFormat{LaTeX2e}
\ProvidesClass{fallen_report}[2014/10/29]
\LoadClass[12pt, a4paper]{article}

\usepackage{tikz}
\usepackage{pgfplots}
\usepackage{fontspec}
\usepackage{polyglossia}
\usepackage{amsmath}
\usepackage{unicode-math}
\setmainfont{FreeSerif.otf}

\setmainlanguage{russian}
\PolyglossiaSetup{russian}{indentfirst=true}

\usepackage{setspace}
\usepackage{fancyhdr}
\usepackage{ucs}

\usepackage[left = 2.5cm, right = 1.5cm, top = 2cm, bottom = 4cm, bindingoffset = 0cm]{geometry}

\usepackage{listings}
\setmonofont{Consolas}
\usepackage{color}

\hyphenpenalty=50
\clubpenalty=10000
\widowpenalty=10000
\righthyphenmin=2

\definecolor{mygreen}{rgb}{0,0.6,0}
\definecolor{mygray}{rgb}{0.5,0.5,0.5}
\definecolor{mymauve}{rgb}{0.58,0,0.82}
\newfontfamily{\consolas}{Consolas}

\lstset{ %
    backgroundcolor=\color{white},   % choose the background color; you must add \usepackage{color} or \usepackage{xcolor}
    basicstyle=\consolas\footnotesize,        % the size of the fonts that are used for the code
    breakatwhitespace=false,         % sets if automatic breaks should only happen at whitespace
    captionpos=b,                    % sets the caption-position to bottom
    commentstyle=\itshape\color{mygreen},    % comment style
    escapeinside={\%*}{*)},          % if you want to add LaTeX within your code
    extendedchars=true,              % lets you use non-ASCII characters; for 8-bits encodings only, does not work with UTF-8
    frame=single,                    % adds a frame around the code
    keepspaces=true,                 % keeps spaces in text, useful for keeping indentation of code (possibly needs columns=flexible)
    keywordstyle=\color{blue},       % keyword style
    morekeywords={*,...},            % if you want to add more keywords to the set
    numbers=none,                    % where to put the line-numbers; possible values are (none, left, right)
    numberstyle=\consolas\color{mygray}, % the style that is used for the line-numbers
    rulecolor=\color{black},         % if not set, the frame-color may be changed on line-breaks within not-black text (e.g. comments (green here))
    showspaces=false,                % show spaces everywhere adding particular underscores; it overrides 'showstringspaces'
    showstringspaces=false,          % underline spaces within strings only
    showtabs=false,                  % show tabs within strings adding particular underscores
    stringstyle=\color{mymauve},     % string literal style
    tabsize=8,                       % sets default tabsize to 2 spaces
    title=\lstname                   % show the filename of files included with \lstinputlisting; also try caption instead of title
    columns=flexible
}

\newcommand{\includecode}[2][c]{\lstinputlisting[caption=, language=#1]{#2}}

\newcommand{\legendoutofgraph}{
    \pgfplotsset{every axis legend/.append style={
            at={(1.02,1)},
            anchor=north west}}
}
\newcommand{\graphlinewidth}[1]{\pgfplotsset{every axis/.append style={line width=#1}}}

\setlength{\parskip}{1em}
\parindent = 1.5cm
\linespread{1.6}

\fancypagestyle{firststyle} {
    \renewcommand{\headrulewidth}{0.0pt}
    \renewcommand{\footrulewidth}{0.0pt}
    \lfoot{\@empty}
    \rfoot{\@empty}
}

\def\@where{\@empty}
\newcommand{\organisation}[1]{\def \@where{#1}}
\renewcommand\maketitle {
    \begin{titlepage}
        \thispagestyle{firststyle}
        \let \footnotesize \small
        \let \footnoterule \relax
        \let \footnote \thanks

        \fancyhead[C]{\small \@where}
        \null\vfil
        \vskip 60\p@
        \begin{center}
            {\Large \@title \par}
            \vskip 7em
        \end{center}
        \begin{flushleft} {
                \large
                \begin{tabular}[t]{l}
                    \@author
                \end{tabular}
            } \end{flushleft}
            \@thanks
            \fancyfoot[C]{\small \@date}
            \vfil\null
        \end{titlepage}

        \setcounter{footnote}{0}

        \global\let\thanks\relax
        \global\let\maketitle\relax
        \global\let\@thanks\@empty
        \global\let\@author\@empty
        \global\let\@date\@empty
        \global\let\@title\@empty
        \global\let\title\relax
        \global\let\author\relax
        \global\let\date\relax
        \global\let\and\relax
    }

    \fancypagestyle{otherpages} {
        \fancyhead{}
        \fancyfoot{}
        \renewcommand{\headrulewidth}{0.0pt}
        \renewcommand{\footrulewidth}{0.0pt}
        \lfoot{\@empty}
        \cfoot{\@empty}
        \rfoot{\thepage}
    }

    \pagestyle{otherpages}

