.class public final Lcom/dbdm/switcher/ProfileAdapter;
.super Landroidx/recyclerview/widget/RecyclerView$Adapter;
.source "ProfileAdapter.kt"


# annotations
.annotation system Ldalvik/annotation/MemberClasses;
    value = {
        Lcom/dbdm/switcher/ProfileAdapter$VH;
    }
.end annotation

.annotation system Ldalvik/annotation/Signature;
    value = {
        "Landroidx/recyclerview/widget/RecyclerView$Adapter<",
        "Lcom/dbdm/switcher/ProfileAdapter$VH;",
        ">;"
    }
.end annotation

.annotation runtime Lkotlin/Metadata;
    d1 = {
        "\u0000B\n\u0002\u0018\u0002\n\u0002\u0018\u0002\n\u0002\u0018\u0002\n\u0000\n\u0002\u0010!\n\u0002\u0018\u0002\n\u0000\n\u0002\u0018\u0002\n\u0002\u0010\u0002\n\u0002\u0008\u0003\n\u0002\u0018\u0002\n\u0000\n\u0002\u0010\u0008\n\u0002\u0008\u0005\n\u0002\u0018\u0002\n\u0002\u0008\u0003\n\u0002\u0010 \n\u0002\u0008\u0002\u0018\u00002\u000c\u0012\u0008\u0012\u00060\u0002R\u00020\u00000\u0001:\u0001\u0019B;\u0012\u000c\u0010\u0003\u001a\u0008\u0012\u0004\u0012\u00020\u00050\u0004\u0012\u0012\u0010\u0006\u001a\u000e\u0012\u0004\u0012\u00020\u0005\u0012\u0004\u0012\u00020\u00080\u0007\u0012\u0012\u0010\t\u001a\u000e\u0012\u0004\u0012\u00020\u0005\u0012\u0004\u0012\u00020\u00080\u0007\u00a2\u0006\u0002\u0010\nJ\u0008\u0010\r\u001a\u00020\u000eH\u0016J\u001c\u0010\u000f\u001a\u00020\u00082\n\u0010\u0010\u001a\u00060\u0002R\u00020\u00002\u0006\u0010\u0011\u001a\u00020\u000eH\u0016J\u001c\u0010\u0012\u001a\u00060\u0002R\u00020\u00002\u0006\u0010\u0013\u001a\u00020\u00142\u0006\u0010\u0015\u001a\u00020\u000eH\u0016J\u0014\u0010\u0016\u001a\u00020\u00082\u000c\u0010\u0017\u001a\u0008\u0012\u0004\u0012\u00020\u00050\u0018R\u001a\u0010\u0006\u001a\u000e\u0012\u0004\u0012\u00020\u0005\u0012\u0004\u0012\u00020\u00080\u0007X\u0082\u0004\u00a2\u0006\u0002\n\u0000R\u001a\u0010\t\u001a\u000e\u0012\u0004\u0012\u00020\u0005\u0012\u0004\u0012\u00020\u00080\u0007X\u0082\u0004\u00a2\u0006\u0002\n\u0000R\u0014\u0010\u0003\u001a\u0008\u0012\u0004\u0012\u00020\u00050\u0004X\u0082\u0004\u00a2\u0006\u0002\n\u0000R\u000e\u0010\u000b\u001a\u00020\u000cX\u0082\u0004\u00a2\u0006\u0002\n\u0000\u00a8\u0006\u001a"
    }
    d2 = {
        "Lcom/dbdm/switcher/ProfileAdapter;",
        "Landroidx/recyclerview/widget/RecyclerView$Adapter;",
        "Lcom/dbdm/switcher/ProfileAdapter$VH;",
        "profiles",
        "",
        "Lcom/dbdm/switcher/AccountProfile;",
        "onActivate",
        "Lkotlin/Function1;",
        "",
        "onDelete",
        "(Ljava/util/List;Lkotlin/jvm/functions/Function1;Lkotlin/jvm/functions/Function1;)V",
        "sdf",
        "Ljava/text/SimpleDateFormat;",
        "getItemCount",
        "",
        "onBindViewHolder",
        "h",
        "pos",
        "onCreateViewHolder",
        "parent",
        "Landroid/view/ViewGroup;",
        "viewType",
        "replace",
        "newData",
        "",
        "VH",
        "app_debug"
    }
    k = 0x1
    mv = {
        0x1,
        0x9,
        0x0
    }
    xi = 0x30
.end annotation


# instance fields
.field private final onActivate:Lkotlin/jvm/functions/Function1;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "Lkotlin/jvm/functions/Function1<",
            "Lcom/dbdm/switcher/AccountProfile;",
            "Lkotlin/Unit;",
            ">;"
        }
    .end annotation
.end field

.field private final onDelete:Lkotlin/jvm/functions/Function1;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "Lkotlin/jvm/functions/Function1<",
            "Lcom/dbdm/switcher/AccountProfile;",
            "Lkotlin/Unit;",
            ">;"
        }
    .end annotation
.end field

.field private final profiles:Ljava/util/List;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "Ljava/util/List<",
            "Lcom/dbdm/switcher/AccountProfile;",
            ">;"
        }
    .end annotation
.end field

.field private final sdf:Ljava/text/SimpleDateFormat;


# direct methods
.method public static synthetic $r8$lambda$I21CGvo6rA7goqlz9cyKOfv8ymU(Lcom/dbdm/switcher/ProfileAdapter;Lcom/dbdm/switcher/AccountProfile;Landroid/view/View;)V
    .locals 0

    invoke-static {p0, p1, p2}, Lcom/dbdm/switcher/ProfileAdapter;->onBindViewHolder$lambda$0(Lcom/dbdm/switcher/ProfileAdapter;Lcom/dbdm/switcher/AccountProfile;Landroid/view/View;)V

    return-void
.end method

.method public static synthetic $r8$lambda$jk9e1sCsB5jwWrcvY0QeIGOpDOM(Lcom/dbdm/switcher/ProfileAdapter;Lcom/dbdm/switcher/AccountProfile;Landroid/view/View;)V
    .locals 0

    invoke-static {p0, p1, p2}, Lcom/dbdm/switcher/ProfileAdapter;->onBindViewHolder$lambda$1(Lcom/dbdm/switcher/ProfileAdapter;Lcom/dbdm/switcher/AccountProfile;Landroid/view/View;)V

    return-void
.end method

.method public constructor <init>(Ljava/util/List;Lkotlin/jvm/functions/Function1;Lkotlin/jvm/functions/Function1;)V
    .locals 3
    .param p1, "profiles"    # Ljava/util/List;
    .param p2, "onActivate"    # Lkotlin/jvm/functions/Function1;
    .param p3, "onDelete"    # Lkotlin/jvm/functions/Function1;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "(",
            "Ljava/util/List<",
            "Lcom/dbdm/switcher/AccountProfile;",
            ">;",
            "Lkotlin/jvm/functions/Function1<",
            "-",
            "Lcom/dbdm/switcher/AccountProfile;",
            "Lkotlin/Unit;",
            ">;",
            "Lkotlin/jvm/functions/Function1<",
            "-",
            "Lcom/dbdm/switcher/AccountProfile;",
            "Lkotlin/Unit;",
            ">;)V"
        }
    .end annotation

    const-string v0, "profiles"

    invoke-static {p1, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    const-string v0, "onActivate"

    invoke-static {p2, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    const-string v0, "onDelete"

    invoke-static {p3, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    .line 17
    invoke-direct {p0}, Landroidx/recyclerview/widget/RecyclerView$Adapter;-><init>()V

    .line 14
    iput-object p1, p0, Lcom/dbdm/switcher/ProfileAdapter;->profiles:Ljava/util/List;

    .line 15
    iput-object p2, p0, Lcom/dbdm/switcher/ProfileAdapter;->onActivate:Lkotlin/jvm/functions/Function1;

    .line 16
    iput-object p3, p0, Lcom/dbdm/switcher/ProfileAdapter;->onDelete:Lkotlin/jvm/functions/Function1;

    .line 28
    new-instance v0, Ljava/text/SimpleDateFormat;

    const-string v1, "dd/MM/yy HH:mm"

    invoke-static {}, Ljava/util/Locale;->getDefault()Ljava/util/Locale;

    move-result-object v2

    invoke-direct {v0, v1, v2}, Ljava/text/SimpleDateFormat;-><init>(Ljava/lang/String;Ljava/util/Locale;)V

    iput-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter;->sdf:Ljava/text/SimpleDateFormat;

    .line 13
    return-void
.end method

.method private static final onBindViewHolder$lambda$0(Lcom/dbdm/switcher/ProfileAdapter;Lcom/dbdm/switcher/AccountProfile;Landroid/view/View;)V
    .locals 1
    .param p0, "this$0"    # Lcom/dbdm/switcher/ProfileAdapter;
    .param p1, "$p"    # Lcom/dbdm/switcher/AccountProfile;
    .param p2, "it"    # Landroid/view/View;

    const-string v0, "this$0"

    invoke-static {p0, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    const-string v0, "$p"

    invoke-static {p1, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    .line 39
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter;->onActivate:Lkotlin/jvm/functions/Function1;

    invoke-interface {v0, p1}, Lkotlin/jvm/functions/Function1;->invoke(Ljava/lang/Object;)Ljava/lang/Object;

    return-void
.end method

.method private static final onBindViewHolder$lambda$1(Lcom/dbdm/switcher/ProfileAdapter;Lcom/dbdm/switcher/AccountProfile;Landroid/view/View;)V
    .locals 1
    .param p0, "this$0"    # Lcom/dbdm/switcher/ProfileAdapter;
    .param p1, "$p"    # Lcom/dbdm/switcher/AccountProfile;
    .param p2, "it"    # Landroid/view/View;

    const-string v0, "this$0"

    invoke-static {p0, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    const-string v0, "$p"

    invoke-static {p1, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    .line 40
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter;->onDelete:Lkotlin/jvm/functions/Function1;

    invoke-interface {v0, p1}, Lkotlin/jvm/functions/Function1;->invoke(Ljava/lang/Object;)Ljava/lang/Object;

    return-void
.end method


# virtual methods
.method public getItemCount()I
    .locals 1

    .line 43
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter;->profiles:Ljava/util/List;

    invoke-interface {v0}, Ljava/util/List;->size()I

    move-result v0

    return v0
.end method

.method public bridge synthetic onBindViewHolder(Landroidx/recyclerview/widget/RecyclerView$ViewHolder;I)V
    .locals 1
    .param p1, "p0"    # Landroidx/recyclerview/widget/RecyclerView$ViewHolder;
    .param p2, "p1"    # I

    .line 13
    move-object v0, p1

    check-cast v0, Lcom/dbdm/switcher/ProfileAdapter$VH;

    invoke-virtual {p0, v0, p2}, Lcom/dbdm/switcher/ProfileAdapter;->onBindViewHolder(Lcom/dbdm/switcher/ProfileAdapter$VH;I)V

    return-void
.end method

.method public onBindViewHolder(Lcom/dbdm/switcher/ProfileAdapter$VH;I)V
    .locals 6
    .param p1, "h"    # Lcom/dbdm/switcher/ProfileAdapter$VH;
    .param p2, "pos"    # I

    const-string v0, "h"

    invoke-static {p1, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    .line 34
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter;->profiles:Ljava/util/List;

    invoke-interface {v0, p2}, Ljava/util/List;->get(I)Ljava/lang/Object;

    move-result-object v0

    check-cast v0, Lcom/dbdm/switcher/AccountProfile;

    .line 35
    .local v0, "p":Lcom/dbdm/switcher/AccountProfile;
    invoke-virtual {p1}, Lcom/dbdm/switcher/ProfileAdapter$VH;->getTvName()Landroid/widget/TextView;

    move-result-object v1

    invoke-virtual {v0}, Lcom/dbdm/switcher/AccountProfile;->getName()Ljava/lang/String;

    move-result-object v2

    check-cast v2, Ljava/lang/CharSequence;

    invoke-virtual {v1, v2}, Landroid/widget/TextView;->setText(Ljava/lang/CharSequence;)V

    .line 36
    invoke-virtual {p1}, Lcom/dbdm/switcher/ProfileAdapter$VH;->getTvSsaid()Landroid/widget/TextView;

    move-result-object v1

    invoke-virtual {v0}, Lcom/dbdm/switcher/AccountProfile;->getSsaid()Ljava/lang/String;

    move-result-object v2

    new-instance v3, Ljava/lang/StringBuilder;

    invoke-direct {v3}, Ljava/lang/StringBuilder;-><init>()V

    const-string v4, "SSAID: "

    invoke-virtual {v3, v4}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v3

    invoke-virtual {v3, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v2

    invoke-virtual {v2}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v2

    check-cast v2, Ljava/lang/CharSequence;

    invoke-virtual {v1, v2}, Landroid/widget/TextView;->setText(Ljava/lang/CharSequence;)V

    .line 37
    invoke-virtual {p1}, Lcom/dbdm/switcher/ProfileAdapter$VH;->getTvUserId()Landroid/widget/TextView;

    move-result-object v1

    invoke-virtual {v0}, Lcom/dbdm/switcher/AccountProfile;->getUserId()Ljava/lang/String;

    move-result-object v2

    if-nez v2, :cond_0

    const-string v2, "(conta nova)"

    :cond_0
    new-instance v3, Ljava/lang/StringBuilder;

    invoke-direct {v3}, Ljava/lang/StringBuilder;-><init>()V

    const-string v4, "user_id: "

    invoke-virtual {v3, v4}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v3

    invoke-virtual {v3, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v2

    invoke-virtual {v2}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v2

    check-cast v2, Ljava/lang/CharSequence;

    invoke-virtual {v1, v2}, Landroid/widget/TextView;->setText(Ljava/lang/CharSequence;)V

    .line 38
    invoke-virtual {p1}, Lcom/dbdm/switcher/ProfileAdapter$VH;->getTvCreated()Landroid/widget/TextView;

    move-result-object v1

    iget-object v2, p0, Lcom/dbdm/switcher/ProfileAdapter;->sdf:Ljava/text/SimpleDateFormat;

    new-instance v3, Ljava/util/Date;

    invoke-virtual {v0}, Lcom/dbdm/switcher/AccountProfile;->getCreatedAt()J

    move-result-wide v4

    invoke-direct {v3, v4, v5}, Ljava/util/Date;-><init>(J)V

    invoke-virtual {v2, v3}, Ljava/text/SimpleDateFormat;->format(Ljava/util/Date;)Ljava/lang/String;

    move-result-object v2

    check-cast v2, Ljava/lang/CharSequence;

    invoke-virtual {v1, v2}, Landroid/widget/TextView;->setText(Ljava/lang/CharSequence;)V

    .line 39
    invoke-virtual {p1}, Lcom/dbdm/switcher/ProfileAdapter$VH;->getBtnActivate()Landroid/widget/Button;

    move-result-object v1

    new-instance v2, Lcom/dbdm/switcher/ProfileAdapter$$ExternalSyntheticLambda0;

    invoke-direct {v2, p0, v0}, Lcom/dbdm/switcher/ProfileAdapter$$ExternalSyntheticLambda0;-><init>(Lcom/dbdm/switcher/ProfileAdapter;Lcom/dbdm/switcher/AccountProfile;)V

    invoke-virtual {v1, v2}, Landroid/widget/Button;->setOnClickListener(Landroid/view/View$OnClickListener;)V

    .line 40
    invoke-virtual {p1}, Lcom/dbdm/switcher/ProfileAdapter$VH;->getBtnDelete()Landroid/widget/Button;

    move-result-object v1

    new-instance v2, Lcom/dbdm/switcher/ProfileAdapter$$ExternalSyntheticLambda1;

    invoke-direct {v2, p0, v0}, Lcom/dbdm/switcher/ProfileAdapter$$ExternalSyntheticLambda1;-><init>(Lcom/dbdm/switcher/ProfileAdapter;Lcom/dbdm/switcher/AccountProfile;)V

    invoke-virtual {v1, v2}, Landroid/widget/Button;->setOnClickListener(Landroid/view/View$OnClickListener;)V

    .line 41
    return-void
.end method

.method public bridge synthetic onCreateViewHolder(Landroid/view/ViewGroup;I)Landroidx/recyclerview/widget/RecyclerView$ViewHolder;
    .locals 1
    .param p1, "p0"    # Landroid/view/ViewGroup;
    .param p2, "p1"    # I

    .line 13
    invoke-virtual {p0, p1, p2}, Lcom/dbdm/switcher/ProfileAdapter;->onCreateViewHolder(Landroid/view/ViewGroup;I)Lcom/dbdm/switcher/ProfileAdapter$VH;

    move-result-object v0

    check-cast v0, Landroidx/recyclerview/widget/RecyclerView$ViewHolder;

    return-object v0
.end method

.method public onCreateViewHolder(Landroid/view/ViewGroup;I)Lcom/dbdm/switcher/ProfileAdapter$VH;
    .locals 4
    .param p1, "parent"    # Landroid/view/ViewGroup;
    .param p2, "viewType"    # I

    const-string v0, "parent"

    invoke-static {p1, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    .line 31
    new-instance v0, Lcom/dbdm/switcher/ProfileAdapter$VH;

    invoke-virtual {p1}, Landroid/view/ViewGroup;->getContext()Landroid/content/Context;

    move-result-object v1

    invoke-static {v1}, Landroid/view/LayoutInflater;->from(Landroid/content/Context;)Landroid/view/LayoutInflater;

    move-result-object v1

    sget v2, Lcom/dbdm/switcher/R$layout;->item_profile:I

    const/4 v3, 0x0

    invoke-virtual {v1, v2, p1, v3}, Landroid/view/LayoutInflater;->inflate(ILandroid/view/ViewGroup;Z)Landroid/view/View;

    move-result-object v1

    const-string v2, "inflate(...)"

    invoke-static {v1, v2}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullExpressionValue(Ljava/lang/Object;Ljava/lang/String;)V

    invoke-direct {v0, p0, v1}, Lcom/dbdm/switcher/ProfileAdapter$VH;-><init>(Lcom/dbdm/switcher/ProfileAdapter;Landroid/view/View;)V

    return-object v0
.end method

.method public final replace(Ljava/util/List;)V
    .locals 2
    .param p1, "newData"    # Ljava/util/List;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "(",
            "Ljava/util/List<",
            "Lcom/dbdm/switcher/AccountProfile;",
            ">;)V"
        }
    .end annotation

    const-string v0, "newData"

    invoke-static {p1, v0}, Lkotlin/jvm/internal/Intrinsics;->checkNotNullParameter(Ljava/lang/Object;Ljava/lang/String;)V

    .line 46
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter;->profiles:Ljava/util/List;

    invoke-interface {v0}, Ljava/util/List;->clear()V

    .line 47
    iget-object v0, p0, Lcom/dbdm/switcher/ProfileAdapter;->profiles:Ljava/util/List;

    move-object v1, p1

    check-cast v1, Ljava/util/Collection;

    invoke-interface {v0, v1}, Ljava/util/List;->addAll(Ljava/util/Collection;)Z

    .line 48
    invoke-virtual {p0}, Lcom/dbdm/switcher/ProfileAdapter;->notifyDataSetChanged()V

    .line 49
    return-void
.end method
