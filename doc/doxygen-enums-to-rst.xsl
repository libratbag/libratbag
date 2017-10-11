<!-- XSLT script to combine the generated output into a single file. 
     If you have xsltproc you could use:
     xsltproc combine.xslt index.xml >all.xml
-->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:output method="text" indent="yes"/>
  <xsl:strip-space elements="detaileddescription briefdescription"/>

  <xsl:template match="/">
<xsl:text>
************
Enumerations
************

This document contains the enums used by libratbag's DBus interface.

</xsl:text>
    <xsl:apply-templates select="//memberdef"/>
  </xsl:template>

  <xsl:template match="memberdef">

<xsl:value-of select="name"/>
---------------------------------------

  <xsl:text>&#xa;</xsl:text>
  <xsl:if test="briefdescription != ''">
  <xsl:value-of select="briefdescription"/>
  </xsl:if>
  <xsl:if test="detaileddescription != ''">
  <xsl:value-of select="detaileddescription"/>
  </xsl:if>

.. cpp:enum:: <xsl:value-of select="name"/>

<xsl:text>&#xa;&#xa;</xsl:text>
<xsl:apply-templates select="./enumvalue"/>

  </xsl:template>

  <xsl:template match="enumvalue">
    .. cpp:enumerator:: <xsl:value-of select="name"/><xsl:text> </xsl:text><xsl:value-of select="initializer"/>
      <xsl:text>&#xa;&#xa;</xsl:text>
      <xsl:if test="briefdescription != ''">
        <xsl:text>        </xsl:text><xsl:value-of select="briefdescription"/>
      </xsl:if>
      <xsl:if test="detaileddescription != ''">
        <xsl:text>        </xsl:text><xsl:value-of select="detaileddescription"/>
      </xsl:if>
      <xsl:text>&#xa;&#xa;</xsl:text>
  </xsl:template>
</xsl:stylesheet>

